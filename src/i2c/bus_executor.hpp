#pragma once

/**
 * @file bus_executor.hpp
 * @brief Single-threaded executor that serializes all I2C access for the provider.
 */

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

/**
 * @brief Snapshot of executor health and queue metrics.
 */
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

/**
 * @brief Serial job executor for one I2C session.
 *
 * BusExecutor is the provider's local shared-bus safety boundary: every EZO bus
 * operation runs through one worker thread, which prevents concurrent access to
 * the underlying session.
 *
 * Threading:
 * `submit()` is safe to call from multiple threads. Jobs execute one at a time
 * on the worker thread.
 *
 * Error handling:
 * Submit timeouts are caller-side deadlines. They do not preempt an in-flight
 * session operation; they only stop the waiting caller from blocking longer.
 */
class BusExecutor {
public:
    using Job = std::function<Status(ISession &session)>;

    explicit BusExecutor(std::unique_ptr<ISession> session);
    ~BusExecutor();

    /** @brief Open the session and start the worker thread. */
    Status start();

    /** @brief Stop the worker thread, cancel pending tasks, and close the session. */
    void stop();

    /** @brief Report whether the executor worker is currently running. */
    bool is_running() const;

    /**
     * @brief Submit one serialized job to the bus worker.
     *
     * @param job_name Short diagnostic name used in metrics and timeout errors
     * @param timeout Caller-side wait deadline
     * @param job Work item executed on the worker thread
     * @return Job result or deadline/cancellation status
     */
    Status submit(const std::string &job_name,
                  std::chrono::milliseconds timeout,
                  Job job);

    /** @brief Return a point-in-time copy of executor metrics. */
    BusExecutorMetrics snapshot_metrics() const;

    /** @brief Access the owned session pointer for diagnostics. */
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
