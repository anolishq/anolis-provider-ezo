#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "i2c/session.hpp"

namespace anolis_provider_ezo::i2c {

struct BusExecutorMetrics {
    uint64_t submitted = 0;
    uint64_t started = 0;
    uint64_t succeeded = 0;
    uint64_t failed = 0;
    uint64_t timed_out = 0;
    uint64_t cancelled = 0;
    size_t queue_depth = 0;
    std::string last_error;
};

class BusExecutor {
public:
    using Job = std::function<Status(ISession &session)>;

    explicit BusExecutor(std::unique_ptr<ISession> session);
    ~BusExecutor();

    Status start();
    void stop();

    bool is_running() const;

    Status submit(const std::string &job_name,
                  std::chrono::milliseconds timeout,
                  Job job);

    BusExecutorMetrics snapshot_metrics() const;
    ISession *session();

private:
    struct Task {
        std::string name;
        Job job;
        std::promise<Status> promise;
        std::atomic<bool> timed_out{false};
    };

    void worker_loop();
    static void safe_set_promise(std::promise<Status> &promise, const Status &status);

    std::unique_ptr<ISession> session_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::shared_ptr<Task>> queue_;
    std::thread worker_;
    bool running_ = false;
    bool stopping_ = false;
    BusExecutorMetrics metrics_;
};

} // namespace anolis_provider_ezo::i2c
