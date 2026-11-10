#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/types.h>
#endif

#include "common/Result.hpp"

namespace eden {

struct EmbeddedRun;

enum class ReadStatus { Line, Timeout, Closed };

#ifdef _WIN32
using ProcessId = unsigned long;
using PipeHandle = void*;
inline PipeHandle invalidPipe() { return nullptr; }
#else
using ProcessId = pid_t;
using PipeHandle = int;
inline PipeHandle invalidPipe() { return -1; }
#endif

class LineReader {
public:
    LineReader() = default;
    explicit LineReader(PipeHandle handle) : fd_(handle) {}
    ~LineReader();
    LineReader(const LineReader&) = delete;
    LineReader& operator=(const LineReader&) = delete;
    LineReader(LineReader&& other) noexcept;
    LineReader& operator=(LineReader&& other) noexcept;

    ReadStatus read(std::string& line, int timeoutMs);
    void close();

private:
    PipeHandle fd_ = invalidPipe();
    std::string buffer_;
    bool eof_ = false;
};

class ChildProcess {
public:
    static Result<std::unique_ptr<ChildProcess>> spawn(const std::vector<std::string>& argv,
                                                       const std::map<std::string, std::string>& env = {},
                                                       const std::string& workingDirectory = "");
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    ProcessId pid() const { return pid_; }
    bool writeAll(const std::string& data);
    void closeStdin();
    ReadStatus readLine(std::string& line, int timeoutMs) { return stdout_.read(line, timeoutMs); }
    ReadStatus readErrorLine(std::string& line, int timeoutMs) { return stderr_.read(line, timeoutMs); }
    bool running();
    int wait(int timeoutMs);
    void terminate(int graceMs = 2000);

private:
    ChildProcess() = default;

    ProcessId pid_ = static_cast<ProcessId>(-1);
#ifdef _WIN32
    void* processHandle_ = nullptr;
    PipeHandle stdinFd_ = nullptr;
#else
    int stdinFd_ = -1;
#endif
    LineReader stdout_;
    LineReader stderr_;
    std::shared_ptr<EmbeddedRun> embedded_;  // set when the sidecar runs inside the app (Android)
    bool reaped_ = false;
    int exitCode_ = -1;
};

}  // namespace eden
