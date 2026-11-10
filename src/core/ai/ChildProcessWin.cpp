#include "ai/ChildProcess.hpp"

// Windows implementation of LineReader and ChildProcess (anonymous pipes + CreateProcessW).
// The POSIX implementation lives in ChildProcess.cpp; exactly one of the two is compiled.
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <map>
#include <string>
#include <thread>

namespace eden {

namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// Quotes one argument following the rules used by CommandLineToArgvW / the MSVC runtime.
std::wstring quoteArgument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;
    std::wstring out = L"\"";
    for (auto it = argument.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != argument.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == argument.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(*it);
        }
    }
    out.push_back(L'"');
    return out;
}

struct CaseInsensitiveLess {
    bool operator()(const std::wstring& left, const std::wstring& right) const {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    }
};

// Builds a Unicode environment block: the parent's environment plus the overrides.
std::wstring buildEnvironmentBlock(const std::map<std::string, std::string>& overrides) {
    std::map<std::wstring, std::wstring, CaseInsensitiveLess> merged;
    LPWCH block = GetEnvironmentStringsW();
    if (block != nullptr) {
        for (const wchar_t* entry = block; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
            const std::wstring text(entry);
            const std::size_t equals = text.find(L'=', 1);  // entries such as "=C:=C:\" start with '='
            if (equals == std::wstring::npos) continue;
            merged[text.substr(0, equals)] = text.substr(equals + 1);
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& [key, value] : overrides) merged[widen(key)] = widen(value);
    std::wstring out;
    for (const auto& [key, value] : merged) {
        out += key;
        out.push_back(L'=');
        out += value;
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

void closeHandle(void*& handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) CloseHandle(static_cast<HANDLE>(handle));
    handle = nullptr;
}

std::string lastErrorText(const char* what) {
    return std::string(what) + " (Windows error " + std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
}

}  // namespace

LineReader::~LineReader() { close(); }

LineReader::LineReader(LineReader&& other) noexcept
    : fd_(other.fd_), buffer_(std::move(other.buffer_)), eof_(other.eof_) {
    other.fd_ = invalidPipe();
}

LineReader& LineReader::operator=(LineReader&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        buffer_ = std::move(other.buffer_);
        eof_ = other.eof_;
        other.fd_ = invalidPipe();
    }
    return *this;
}

void LineReader::close() { closeHandle(fd_); }

ReadStatus LineReader::read(std::string& line, int timeoutMs) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs);
    while (true) {
        const std::size_t newline = buffer_.find('\n');
        if (newline != std::string::npos) {
            line = buffer_.substr(0, newline);
            buffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();  // text-mode children write CRLF
            return ReadStatus::Line;
        }
        if (eof_ || fd_ == nullptr) {
            if (!buffer_.empty()) {
                line = std::move(buffer_);
                buffer_.clear();
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return ReadStatus::Line;
            }
            return ReadStatus::Closed;
        }
        DWORD available = 0;
        if (!PeekNamedPipe(static_cast<HANDLE>(fd_), nullptr, 0, nullptr, &available, nullptr)) {
            eof_ = true;  // broken pipe: the child closed its end
            continue;
        }
        if (available > 0) {
            char chunk[4096];
            const DWORD wanted = std::min<DWORD>(available, static_cast<DWORD>(sizeof(chunk)));
            DWORD got = 0;
            if (ReadFile(static_cast<HANDLE>(fd_), chunk, wanted, &got, nullptr) && got > 0) {
                buffer_.append(chunk, static_cast<std::size_t>(got));
            } else {
                eof_ = true;
            }
            continue;
        }
        if (timeoutMs >= 0 && Clock::now() >= deadline) return ReadStatus::Timeout;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

Result<std::unique_ptr<ChildProcess>> ChildProcess::spawn(const std::vector<std::string>& argv,
                                                          const std::map<std::string, std::string>& env,
                                                          const std::string& workingDirectory) {
    if (argv.empty()) return makeError(ErrorCode::InvalidArgument, "empty command");

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    security.lpSecurityDescriptor = nullptr;

    HANDLE inputRead = nullptr, inputWrite = nullptr;
    HANDLE outputRead = nullptr, outputWrite = nullptr;
    HANDLE errorRead = nullptr, errorWrite = nullptr;
    auto closeAll = [&]() {
        for (HANDLE* handle : {&inputRead, &inputWrite, &outputRead, &outputWrite, &errorRead, &errorWrite}) {
            if (*handle != nullptr) CloseHandle(*handle);
            *handle = nullptr;
        }
    };
    if (!CreatePipe(&inputRead, &inputWrite, &security, 0) || !CreatePipe(&outputRead, &outputWrite, &security, 0) ||
        !CreatePipe(&errorRead, &errorWrite, &security, 0)) {
        const std::string message = lastErrorText("pipe failed");
        closeAll();
        return makeError(ErrorCode::Io, message);
    }
    // The parent's ends must not be inherited by the child.
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errorRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring commandLine;
    for (const std::string& argument : argv) {
        if (!commandLine.empty()) commandLine.push_back(L' ');
        commandLine += quoteArgument(widen(argument));
    }
    std::wstring environmentBlock;
    if (!env.empty()) environmentBlock = buildEnvironmentBlock(env);
    const std::wstring directory = widen(workingDirectory);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inputRead;
    startup.hStdOutput = outputWrite;
    startup.hStdError = errorWrite;
    PROCESS_INFORMATION information{};

    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                        env.empty() ? nullptr : static_cast<LPVOID>(environmentBlock.data()),
                                        directory.empty() ? nullptr : directory.c_str(), &startup, &information);
    if (!created) {
        const std::string message = lastErrorText(("cannot start " + argv[0]).c_str());
        closeAll();
        return makeError(ErrorCode::Io, message);
    }
    CloseHandle(information.hThread);
    // The child's ends now belong to the child.
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    CloseHandle(errorWrite);

    std::unique_ptr<ChildProcess> process(new ChildProcess());
    process->pid_ = static_cast<ProcessId>(information.dwProcessId);
    process->processHandle_ = information.hProcess;
    process->stdinFd_ = inputWrite;
    process->stdout_ = LineReader(outputRead);
    process->stderr_ = LineReader(errorRead);
    return process;
}

ChildProcess::~ChildProcess() {
    closeStdin();
    if (processHandle_ != nullptr && !reaped_) terminate(500);
    closeHandle(processHandle_);
}

bool ChildProcess::writeAll(const std::string& data) {
    if (stdinFd_ == nullptr) return false;
    std::size_t written = 0;
    while (written < data.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - written, 1u << 20));
        DWORD count = 0;
        if (!WriteFile(static_cast<HANDLE>(stdinFd_), data.data() + written, chunk, &count, nullptr)) return false;
        if (count == 0) return false;
        written += static_cast<std::size_t>(count);
    }
    return true;
}

void ChildProcess::closeStdin() { closeHandle(stdinFd_); }

bool ChildProcess::running() {
    if (reaped_ || processHandle_ == nullptr) return false;
    if (WaitForSingleObject(static_cast<HANDLE>(processHandle_), 0) == WAIT_TIMEOUT) return true;
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(processHandle_), &code);
    reaped_ = true;
    exitCode_ = static_cast<int>(code);
    return false;
}

int ChildProcess::wait(int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (running()) {
        if (std::chrono::steady_clock::now() >= deadline) return -1;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return exitCode_;
}

void ChildProcess::terminate(int graceMs) {
    (void)graceMs;  // Windows has no SIGTERM equivalent for console-less children; stop it directly.
    if (!running()) return;
    TerminateProcess(static_cast<HANDLE>(processHandle_), 1);
    wait(2000);
}

}  // namespace eden

#endif  // _WIN32
