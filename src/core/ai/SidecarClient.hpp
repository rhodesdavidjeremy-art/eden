#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ai/ChildProcess.hpp"
#include "common/Json.hpp"
#include "common/Result.hpp"

namespace eden {

struct SidecarOptions {
    std::vector<std::string> command;
    std::map<std::string, std::string> environment;
    std::string workingDirectory;
    int startupTimeoutMs = 15000;
    int defaultTimeoutMs = 120000;
    bool autoRestart = true;
    std::function<void(const std::string&)> onStderr;
};

struct CallOptions {
    int timeoutMs = 0;
    std::function<void(const Json&)> onEvent;
    const std::atomic<bool>* cancelFlag = nullptr;
};

Error errorFromSidecar(const Json& error);

class SidecarClient {
public:
    using Completion = std::function<void(Result<Json>)>;

    explicit SidecarClient(SidecarOptions options);
    ~SidecarClient();
    SidecarClient(const SidecarClient&) = delete;
    SidecarClient& operator=(const SidecarClient&) = delete;

    Status start();
    void stop();
    bool running() const { return running_.load(); }
    int restarts() const { return restarts_.load(); }

    Result<Json> call(const std::string& method, const Json& params = Json::object(), const CallOptions& options = {});
    void callAsync(const std::string& method, const Json& params, CallOptions options, Completion done);

private:
    struct Pending {
        std::mutex mutex;
        std::condition_variable ready;
        bool done = false;
        Json response;
        std::string failure;
        std::function<void(const Json&)> onEvent;
    };

    Status startLocked();
    Status ensureRunning();
    void readerLoop(ChildProcess* process);
    void stderrLoop(ChildProcess* process);
    void handleLine(const std::string& line);
    void failAll(const std::string& reason);
    bool send(const Json& message);
    void sendCancel(const std::string& target);

    SidecarOptions options_;
    std::mutex lifecycleMutex_;
    std::mutex writeMutex_;
    std::mutex pendingMutex_;
    std::map<std::string, std::shared_ptr<Pending>> pending_;
    std::unique_ptr<ChildProcess> process_;
    std::thread readerThread_;
    std::thread stderrThread_;
    std::vector<std::thread> asyncThreads_;
    std::mutex asyncMutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<int> restarts_{0};
    std::atomic<std::uint64_t> nextId_{1};
    bool everStarted_ = false;
};

}  // namespace eden
