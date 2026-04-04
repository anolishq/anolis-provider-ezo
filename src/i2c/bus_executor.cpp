/**
 * @file bus_executor.cpp
 * @brief Implementation of the provider's serialized I2C execution model.
 *
 * The executor owns one session and one worker thread. All bus work is queued
 * and executed in order so higher-level handlers never touch the I2C transport
 * concurrently.
 */

#include "i2c/bus_executor.hpp"

#include <utility>

namespace anolis_provider_ezo::i2c {
namespace {

Status make_status(StatusCode code, const std::string &message) {
    return Status{code, message};
}

} // namespace

BusExecutor::BusExecutor(std::unique_ptr<ISession> session)
    : session_(std::move(session)) {}

BusExecutor::~BusExecutor() {
    stop();
}

Status BusExecutor::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(running_) {
        return Status::ok();
    }
    if(!session_) {
        return make_status(StatusCode::Internal, "missing session");
    }

    Status open_status = session_->open();
    if(!open_status.is_ok()) {
        metrics_.last_error = open_status.message;
        return open_status;
    }

    stopping_ = false;
    running_ = true;
    worker_ = std::thread(&BusExecutor::worker_loop, this);
    return Status::ok();
}

void BusExecutor::stop() {
    std::queue<std::shared_ptr<Task>> pending;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!running_) {
            if(session_) {
                session_->close();
            }
            return;
        }

        stopping_ = true;
        pending.swap(queue_);
        metrics_.queue_depth = 0;

        while(!pending.empty()) {
            auto task = pending.front();
            pending.pop();
            safe_set_promise(task->promise,
                             make_status(StatusCode::Cancelled,
                                         "executor stopped before task execution"));
            ++metrics_.cancelled;
        }
    }

    cv_.notify_all();

    if(worker_.joinable()) {
        worker_.join();
    }

    if(session_) {
        session_->close();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    stopping_ = false;
}

bool BusExecutor::is_running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

Status BusExecutor::submit(const std::string &job_name,
                           std::chrono::milliseconds timeout,
                           Job job) {
    auto task = std::make_shared<Task>();
    task->name = job_name;
    task->job = std::move(job);
    auto future = task->promise.get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!running_ || stopping_) {
            return make_status(StatusCode::Unavailable, "executor is not running");
        }
        ++metrics_.submitted;
        queue_.push(task);
        metrics_.queue_depth = queue_.size();
    }

    cv_.notify_one();

    // Timeouts apply to the waiting caller, not to the underlying I2C transfer.
    // If the worker has already started the task, the caller may receive a
    // deadline error even though the session operation finishes later.
    if(timeout.count() > 0) {
        const std::future_status wait_status = future.wait_for(timeout);
        if(wait_status != std::future_status::ready) {
            task->timed_out.store(true);
            std::lock_guard<std::mutex> lock(mutex_);
            ++metrics_.timed_out;
            metrics_.last_error = "job '" + job_name + "' timed out";
            return make_status(StatusCode::DeadlineExceeded,
                               "job '" + job_name + "' timed out");
        }
    }

    return future.get();
}

BusExecutorMetrics BusExecutor::snapshot_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

ISession *BusExecutor::session() {
    std::lock_guard<std::mutex> lock(mutex_);
    return session_.get();
}

void BusExecutor::worker_loop() {
    while(true) {
        std::shared_ptr<Task> task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });

            if(stopping_ && queue_.empty()) {
                return;
            }

            task = queue_.front();
            queue_.pop();
            metrics_.queue_depth = queue_.size();
            ++metrics_.started;
        }

        // If the caller timed out before this task started, skip the job rather
        // than issuing an unnecessary bus transaction.
        if(task->timed_out.load()) {
            safe_set_promise(task->promise,
                             make_status(StatusCode::Cancelled,
                                         "task skipped because caller timed out"));
            std::lock_guard<std::mutex> lock(mutex_);
            ++metrics_.cancelled;
            continue;
        }

        Status status = task->job(*session_);

        if(!task->timed_out.load()) {
            std::lock_guard<std::mutex> lock(mutex_);
            if(status.is_ok()) {
                ++metrics_.succeeded;
            } else {
                ++metrics_.failed;
                metrics_.last_error = status.message;
            }
        }

        safe_set_promise(task->promise, status);
    }
}

void BusExecutor::safe_set_promise(std::promise<Status> &promise,
                                   const Status &status) {
    try {
        promise.set_value(status);
    } catch(const std::future_error &) {
        // Promise already satisfied/caller no longer waiting.
    }
}

} // namespace anolis_provider_ezo::i2c
