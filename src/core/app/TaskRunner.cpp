#include "app/TaskRunner.hpp"

#include <algorithm>

namespace eden {

TaskRunner::TaskRunner(std::size_t workers, Dispatcher dispatcher) : dispatcher_(std::move(dispatcher)) {
    const std::size_t count = std::max<std::size_t>(1, workers);
    for (std::size_t i = 0; i < count; ++i) workers_.emplace_back([this] { workerLoop(); });
}

TaskRunner::~TaskRunner() { shutdown(); }

void TaskRunner::post(std::function<void()> callback) {
    if (dispatcher_) {
        dispatcher_(std::move(callback));
    } else {
        callback();
    }
}

void TaskRunner::setProgressHandler(ProgressHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    progressHandler_ = std::move(handler);
}

TaskId TaskRunner::submit(std::string label, Work work, Done done) {
    auto task = std::make_shared<Task>();
    task->work = std::move(work);
    task->done = std::move(done);
    task->cancelFlag = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);
    task->info.id = nextId_++;
    task->info.label = std::move(label);
    active_[task->info.id] = task;
    queue_.push_back(task);
    wake_.notify_one();
    return task->info.id;
}

bool TaskRunner::cancel(TaskId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_.find(id);
    if (it == active_.end()) return false;
    it->second->cancelFlag->store(true);
    it->second->info.cancelled = true;
    return true;
}

void TaskRunner::cancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, task] : active_) {
        task->cancelFlag->store(true);
        task->info.cancelled = true;
    }
}

void TaskRunner::waitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && busy_ == 0; });
}

void TaskRunner::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
        for (auto& [id, task] : active_) task->cancelFlag->store(true);
        wake_.notify_all();
    }
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

std::vector<TaskInfo> TaskRunner::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskInfo> out;
    for (const auto& [id, task] : active_) out.push_back(task->info);
    return out;
}

void TaskRunner::report(TaskId id, double fraction, const std::string& message) {
    TaskInfo info;
    ProgressHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_.find(id);
        if (it == active_.end()) return;
        it->second->info.progress = std::max(0.0, std::min(1.0, fraction));
        it->second->info.message = message;
        info = it->second->info;
        handler = progressHandler_;
    }
    if (handler) post([handler, info] { handler(info); });
}

void TaskRunner::workerLoop() {
    while (true) {
        std::shared_ptr<Task> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                if (stopping_) return;
                continue;
            }
            task = queue_.front();
            queue_.pop_front();
            task->info.running = true;
            ++busy_;
        }
        Status outcome = Status::success();
        if (task->cancelFlag->load()) {
            outcome = makeError(ErrorCode::Cancelled, "task cancelled before it started");
        } else {
            TaskContext context(task->info.id, task->cancelFlag,
                                [this](TaskId id, double fraction, const std::string& message) { report(id, fraction, message); });
            try {
                outcome = task->work(context);
            } catch (const std::exception& error) {
                outcome = makeError(ErrorCode::Internal, error.what());
            } catch (...) {
                outcome = makeError(ErrorCode::Internal, "task threw an unknown exception");
            }
        }
        if (task->done) {
            Done done = task->done;
            post([done, outcome] { done(outcome); });
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_.erase(task->info.id);
            --busy_;
            idle_.notify_all();
        }
    }
}

}  // namespace eden
