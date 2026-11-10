#include "ai/ChildProcess.hpp"

#include "ai/EmbeddedPython.hpp"

// POSIX implementation. The Windows implementation lives in ChildProcessWin.cpp.
#ifndef _WIN32

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

namespace eden {

namespace {

void ignoreBrokenPipes() {
    static const bool done = [] {
        signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)done;
}

}  // namespace

LineReader::~LineReader() { close(); }

LineReader::LineReader(LineReader&& other) noexcept
    : fd_(other.fd_), buffer_(std::move(other.buffer_)), eof_(other.eof_) {
    other.fd_ = -1;
}

LineReader& LineReader::operator=(LineReader&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        buffer_ = std::move(other.buffer_);
        eof_ = other.eof_;
        other.fd_ = -1;
    }
    return *this;
}

void LineReader::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

ReadStatus LineReader::read(std::string& line, int timeoutMs) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const std::size_t newline = buffer_.find('\n');
        if (newline != std::string::npos) {
            line = buffer_.substr(0, newline);
            buffer_.erase(0, newline + 1);
            return ReadStatus::Line;
        }
        if (eof_ || fd_ < 0) {
            if (!buffer_.empty()) {
                line = std::move(buffer_);
                buffer_.clear();
                return ReadStatus::Line;
            }
            return ReadStatus::Closed;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0 && timeoutMs >= 0) return ReadStatus::Timeout;
        pollfd descriptor{fd_, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, timeoutMs < 0 ? -1 : static_cast<int>(remaining));
        if (ready < 0) {
            if (errno == EINTR) continue;
            eof_ = true;
            continue;
        }
        if (ready == 0) return ReadStatus::Timeout;
        char chunk[4096];
        const ssize_t got = ::read(fd_, chunk, sizeof(chunk));
        if (got > 0) {
            buffer_.append(chunk, static_cast<std::size_t>(got));
        } else if (got == 0) {
            eof_ = true;
        } else if (errno != EINTR && errno != EAGAIN) {
            eof_ = true;
        }
    }
}

Result<std::unique_ptr<ChildProcess>> ChildProcess::spawn(const std::vector<std::string>& argv,
                                                          const std::map<std::string, std::string>& env,
                                                          const std::string& workingDirectory) {
    if (argv.empty()) return makeError(ErrorCode::InvalidArgument, "empty command");
    ignoreBrokenPipes();
    int input[2];
    int output[2];
    int error[2];
    if (pipe2(input, O_CLOEXEC) != 0) return makeError(ErrorCode::Io, std::string("pipe failed: ") + std::strerror(errno));
    if (pipe2(output, O_CLOEXEC) != 0) {
        ::close(input[0]);
        ::close(input[1]);
        return makeError(ErrorCode::Io, std::string("pipe failed: ") + std::strerror(errno));
    }
    if (pipe2(error, O_CLOEXEC) != 0) {
        for (int fd : {input[0], input[1], output[0], output[1]}) ::close(fd);
        return makeError(ErrorCode::Io, std::string("pipe failed: ") + std::strerror(errno));
    }
#ifdef EDEN_EMBED_PYTHON
    if (argv[0] == kEmbeddedPythonCommand) {
        auto run = startEmbeddedPython(argv, env, input[0], output[1], error[1]);
        if (!run.ok()) {
            for (int fd : {input[0], input[1], output[0], output[1], error[0], error[1]}) ::close(fd);
            return run.error();
        }
        std::unique_ptr<ChildProcess> process(new ChildProcess());
        process->embedded_ = run.value();
        process->stdinFd_ = input[1];
        process->stdout_ = LineReader(output[0]);
        process->stderr_ = LineReader(error[0]);
        return process;
    }
#endif
    std::vector<std::string> argumentStorage = argv;
    std::vector<char*> arguments;
    for (std::string& argument : argumentStorage) arguments.push_back(argument.data());
    arguments.push_back(nullptr);
    std::vector<std::string> environmentStorage;
    for (const auto& [key, value] : env) environmentStorage.push_back(key + "=" + value);
    std::vector<char*> environment;
    for (std::string& entry : environmentStorage) environment.push_back(entry.data());

    const pid_t child = fork();
    if (child < 0) {
        for (int fd : {input[0], input[1], output[0], output[1], error[0], error[1]}) ::close(fd);
        return makeError(ErrorCode::Io, std::string("fork failed: ") + std::strerror(errno));
    }
    if (child == 0) {
        dup2(input[0], STDIN_FILENO);
        dup2(output[1], STDOUT_FILENO);
        dup2(error[1], STDERR_FILENO);
        if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0) _exit(126);
        for (char* entry : environment) putenv(entry);
        execvp(arguments[0], arguments.data());
        _exit(127);
    }
    ::close(input[0]);
    ::close(output[1]);
    ::close(error[1]);
    std::unique_ptr<ChildProcess> process(new ChildProcess());
    process->pid_ = child;
    process->stdinFd_ = input[1];
    process->stdout_ = LineReader(output[0]);
    process->stderr_ = LineReader(error[0]);
    return process;
}

ChildProcess::~ChildProcess() {
    closeStdin();
    if (embedded_) return;  // the interpreter thread ends by itself once its stdin is closed
    if (pid_ > 0 && !reaped_) terminate(500);
}

bool ChildProcess::writeAll(const std::string& data) {
    if (stdinFd_ < 0) return false;
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t count = ::write(stdinFd_, data.data() + written, data.size() - written);
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    return true;
}

void ChildProcess::closeStdin() {
    if (stdinFd_ >= 0) {
        ::close(stdinFd_);
        stdinFd_ = -1;
    }
}

bool ChildProcess::running() {
    if (embedded_) {
        if (reaped_) return false;
        if (!embedded_->finished.load()) return true;
        reaped_ = true;
        exitCode_ = embedded_->exitCode.load();
        return false;
    }
    if (reaped_ || pid_ <= 0) return false;
    int status = 0;
    const pid_t result = waitpid(pid_, &status, WNOHANG);
    if (result == 0) return true;
    reaped_ = true;
    exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
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
    if (embedded_) {
        // A thread cannot be killed: close stdin so the sidecar loop ends, then give it a moment and let go.
        closeStdin();
        if (wait(graceMs) < 0) {
            reaped_ = true;
            exitCode_ = -1;
        }
        return;
    }
    if (!running()) return;
    kill(pid_, SIGTERM);
    if (wait(graceMs) >= 0 || reaped_) return;
    kill(pid_, SIGKILL);
    wait(2000);
}

}  // namespace eden

#endif  // !_WIN32
