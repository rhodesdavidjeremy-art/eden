#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/Result.hpp"

namespace eden {

using TaskId = std::uint64_t;
using Dispatcher = std::function<void(std::function<void()>)>;

struct TaskInfo {
    TaskId id = 0;
    std::string label;
    double progress = 0.0;
    std::string message;
    bool running = false;
    bool cancelled = false;
};

class TaskContext {
public:
    TaskContext(TaskId id, std::shared_ptr<std::atomic<bool>> flag, std::function<void(TaskId, double, const std::string&)> report)
        : id_(id), flag_(std::move(flag)), report_(std::move(report)) {}

    TaskId id() const { return id_; }
    bool cancelled() const { return flag_->load(); }
    const std::atomic<bool>* cancelFlag() const { return flag_.get(); }
    void progress(double fraction, const std::string& message) const { report_(id_, fraction, message); }

private:
    TaskId id_;
    std::shared_ptr<std::atomic<bool>> flag_;
    std::function<void(TaskId, double, const std::string&)> report_;
};

class TaskRunner {
public:
    using Work = std::function<Status(TaskContext&)>;
    using Done = std::function<void(const Status&)>;
    using ProgressHandler = std::function<void(const TaskInfo&)>;

    explicit TaskRunner(std::size_t workers = 2, Dispatcher dispatcher = nullptr);
    ~TaskRunner();
    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    TaskId submit(std::string label, Work work, Done done = nullptr);
    bool cancel(TaskId id);
    void cancelAll();
    void waitIdle();
    void shutdown();
    std::vector<TaskInfo> snapshot() const;
    void setProgressHandler(ProgressHandler handler);

private:
    struct Task {
        TaskInfo info;
        Work work;
        Done done;
        std::shared_ptr<std::atomic<bool>> cancelFlag;
    };

    void workerLoop();
    void post(std::function<void()> callback);
    void report(TaskId id, double fraction, const std::string& message);

    Dispatcher dispatcher_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<std::shared_ptr<Task>> queue_;
    std::map<TaskId, std::shared_ptr<Task>> active_;
    std::vector<std::thread> workers_;
    ProgressHandler progressHandler_;
    TaskId nextId_ = 1;
    std::size_t busy_ = 0;
    bool stopping_ = false;
};

}  // namespace eden
