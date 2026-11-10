#include "ai/SidecarClient.hpp"

#include <chrono>

#include "common/Log.hpp"

namespace eden {

namespace {

bool trimLine(std::string& line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return !line.empty();
}

}  // namespace

Error errorFromSidecar(const Json& error) {
    const std::string code = error.getString("code", "internal");
    const std::string message = error.getString("message", "sidecar error");
    ErrorCode mapped = ErrorCode::Internal;
    if (code == "invalid_argument") mapped = ErrorCode::InvalidArgument;
    else if (code == "not_found") mapped = ErrorCode::NotFound;
    else if (code == "parse") mapped = ErrorCode::Parse;
    else if (code == "cancelled") mapped = ErrorCode::Cancelled;
    else if (code == "auth" || code == "config") mapped = ErrorCode::Auth;
    else if (code == "rate_limit") mapped = ErrorCode::RateLimited;
    else if (code == "network") mapped = ErrorCode::Network;
    else if (code == "provider") mapped = ErrorCode::Provider;
    return Error{mapped, message};
}

SidecarClient::SidecarClient(SidecarOptions options) : options_(std::move(options)) {}

SidecarClient::~SidecarClient() { stop(); }

Status SidecarClient::start() {
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    return startLocked();
}

Status SidecarClient::startLocked() {
    if (running_.load()) return Status::success();
    if (options_.command.empty()) return makeError(ErrorCode::InvalidArgument, "sidecar command is empty");
    if (readerThread_.joinable()) readerThread_.join();
    if (stderrThread_.joinable()) stderrThread_.join();
    process_.reset();
    auto spawned = ChildProcess::spawn(options_.command, options_.environment, options_.workingDirectory);
    if (!spawned.ok()) return spawned.error();
    process_ = std::move(spawned.value());
    stopping_ = false;
    running_ = true;
    if (everStarted_) restarts_++;
    everStarted_ = true;
    ChildProcess* raw = process_.get();
    readerThread_ = std::thread([this, raw] { readerLoop(raw); });
    stderrThread_ = std::thread([this, raw] { stderrLoop(raw); });

    CallOptions ping;
    ping.timeoutMs = options_.startupTimeoutMs;
    lifecycleMutex_.unlock();
    auto reply = call("ping", Json::object(), ping);
    lifecycleMutex_.lock();
    if (!reply.ok()) {
        stopping_ = true;
        process_->terminate(500);
        running_ = false;
        return makeError(ErrorCode::Io, "sidecar did not answer startup ping: " + reply.error().message);
    }
    return Status::success();
}

Status SidecarClient::ensureRunning() {
    if (running_.load()) return Status::success();
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    if (running_.load()) return Status::success();
    if (everStarted_ && !options_.autoRestart) return makeError(ErrorCode::Io, "sidecar is not running");
    if (stopping_.load() && !everStarted_) return makeError(ErrorCode::Io, "sidecar was stopped");
    return startLocked();
}

void SidecarClient::stop() {
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(asyncMutex_);
        threads.swap(asyncThreads_);
    }
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        stopping_ = true;
        if (process_) {
            Json shutdown = Json::object({{"id", Json("shutdown")}, {"method", Json("shutdown")}, {"params", Json::object()}});
            send(shutdown);
            process_->closeStdin();
            if (process_->wait(3000) < 0) process_->terminate(1000);
        }
        running_ = false;
        if (readerThread_.joinable()) readerThread_.join();
        if (stderrThread_.joinable()) stderrThread_.join();
        process_.reset();
        everStarted_ = false;
    }
    for (std::thread& thread : threads) {
        if (thread.joinable()) thread.join();
    }
    failAll("sidecar stopped");
}

bool SidecarClient::send(const Json& message) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (!process_) return false;
    return process_->writeAll(message.dump() + "\n");
}

void SidecarClient::sendCancel(const std::string& target) {
    const std::string id = "x" + std::to_string(nextId_++);
    send(Json::object({{"id", Json(id)}, {"method", Json("cancel")},
                       {"params", Json::object({{"target", Json(target)}})}}));
}

void SidecarClient::readerLoop(ChildProcess* process) {
    std::string line;
    while (true) {
        const ReadStatus status = process->readLine(line, 500);
        if (status == ReadStatus::Timeout) continue;
        if (status == ReadStatus::Closed) break;
        if (!trimLine(line)) continue;
        handleLine(line);
    }
    running_ = false;
    failAll(stopping_.load() ? "sidecar stopped" : "sidecar process exited unexpectedly");
}

void SidecarClient::stderrLoop(ChildProcess* process) {
    std::string line;
    while (true) {
        const ReadStatus status = process->readErrorLine(line, 500);
        if (status == ReadStatus::Timeout) {
            if (!running_.load()) {
                std::string rest;
                if (process->readErrorLine(rest, 0) != ReadStatus::Line) break;
                line = rest;
            } else {
                continue;
            }
        } else if (status == ReadStatus::Closed) {
            break;
        }
        if (options_.onStderr) {
            options_.onStderr(line);
        } else {
            logf(LogLevel::Warn, "sidecar", line);
        }
    }
}

void SidecarClient::handleLine(const std::string& line) {
    auto parsed = Json::parse(line);
    if (!parsed.ok() || !parsed.value().isObject()) {
        logf(LogLevel::Warn, "sidecar", "unparseable message: ", line);
        return;
    }
    const Json& message = parsed.value();
    const std::string id = message.getString("id");
    std::shared_ptr<Pending> pending;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        auto it = pending_.find(id);
        if (it != pending_.end()) pending = it->second;
    }
    if (!pending) return;
    if (message.contains("event")) {
        std::function<void(const Json&)> handler;
        {
            std::lock_guard<std::mutex> lock(pending->mutex);
            handler = pending->onEvent;
        }
        if (handler) handler(message);
        return;
    }
    if (message.contains("ok")) {
        std::lock_guard<std::mutex> lock(pending->mutex);
        pending->response = message;
        pending->done = true;
        pending->ready.notify_all();
    }
}

void SidecarClient::failAll(const std::string& reason) {
    std::map<std::string, std::shared_ptr<Pending>> drained;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        drained.swap(pending_);
    }
    for (auto& [id, pending] : drained) {
        std::lock_guard<std::mutex> lock(pending->mutex);
        if (!pending->done) {
            pending->failure = reason;
            pending->done = true;
            pending->ready.notify_all();
        }
    }
}

Result<Json> SidecarClient::call(const std::string& method, const Json& params, const CallOptions& options) {
    if (method != "ping" || !running_.load()) {
        Status ready = ensureRunning();
        if (!ready.ok()) return ready.error();
    }
    const std::string id = "r" + std::to_string(nextId_++);
    auto pending = std::make_shared<Pending>();
    pending->onEvent = options.onEvent;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_[id] = pending;
    }
    Json request = Json::object({{"id", Json(id)}, {"method", Json(method)}, {"params", params}});
    if (!send(request)) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.erase(id);
        return makeError(ErrorCode::Io, "could not write to sidecar");
    }
    const int timeoutMs = options.timeoutMs > 0 ? options.timeoutMs : options_.defaultTimeoutMs;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool cancelSent = false;
    auto cancelDeadline = std::chrono::steady_clock::time_point::max();
    std::unique_lock<std::mutex> lock(pending->mutex);
    while (!pending->done) {
        pending->ready.wait_for(lock, std::chrono::milliseconds(40));
        if (pending->done) break;
        const auto now = std::chrono::steady_clock::now();
        if (options.cancelFlag != nullptr && options.cancelFlag->load() && !cancelSent) {
            cancelSent = true;
            cancelDeadline = now + std::chrono::seconds(5);
            lock.unlock();
            sendCancel(id);
            lock.lock();
        }
        const bool timedOut = now >= deadline;
        if (timedOut || now >= cancelDeadline) {
            lock.unlock();
            if (!cancelSent) sendCancel(id);
            {
                std::lock_guard<std::mutex> pendingLock(pendingMutex_);
                pending_.erase(id);
            }
            return makeError(timedOut && !cancelSent ? ErrorCode::Timeout : ErrorCode::Cancelled,
                             timedOut && !cancelSent ? "sidecar call timed out: " + method
                                                      : "sidecar call cancelled: " + method);
        }
    }
    const std::string failure = pending->failure;
    const Json response = pending->response;
    lock.unlock();
    {
        std::lock_guard<std::mutex> pendingLock(pendingMutex_);
        pending_.erase(id);
    }
    if (!failure.empty()) return makeError(ErrorCode::Io, failure);
    if (response.getBool("ok")) return response.get("result");
    return errorFromSidecar(response.get("error"));
}

void SidecarClient::callAsync(const std::string& method, const Json& params, CallOptions options, Completion done) {
    std::lock_guard<std::mutex> lock(asyncMutex_);
    asyncThreads_.emplace_back([this, method, params, options = std::move(options), done = std::move(done)]() mutable {
        Result<Json> outcome = call(method, params, options);
        if (done) done(std::move(outcome));
    });
}

}  // namespace eden
