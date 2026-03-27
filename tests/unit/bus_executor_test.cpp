#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "i2c/bus_executor.hpp"
#include "i2c/session.hpp"

namespace {

using anolis_provider_ezo::i2c::BusExecutor;
using anolis_provider_ezo::i2c::NoopSession;
using anolis_provider_ezo::i2c::Status;
using anolis_provider_ezo::i2c::StatusCode;

TEST(BusExecutorTest, ProcessesJobsInSubmissionOrder) {
    BusExecutor executor(std::make_unique<NoopSession>("mock://executor-order"));
    ASSERT_TRUE(executor.start().is_ok());

    std::vector<int> order;
    std::mutex order_mutex;

    for(int i = 0; i < 5; ++i) {
        const Status status = executor.submit(
            "job-" + std::to_string(i),
            std::chrono::milliseconds(250),
            [&order, &order_mutex, i](anolis_provider_ezo::i2c::ISession &) {
                std::lock_guard<std::mutex> lock(order_mutex);
                order.push_back(i);
                return Status::ok();
            });
        ASSERT_TRUE(status.is_ok()) << status.message;
    }

    executor.stop();

    ASSERT_EQ(order.size(), 5U);
    for(int i = 0; i < 5; ++i) {
        EXPECT_EQ(order[static_cast<std::size_t>(i)], i);
    }

    const auto metrics = executor.snapshot_metrics();
    EXPECT_EQ(metrics.submitted, 5U);
    EXPECT_EQ(metrics.started, 5U);
    EXPECT_EQ(metrics.succeeded, 5U);
    EXPECT_EQ(metrics.failed, 0U);
}

TEST(BusExecutorTest, ReturnsDeadlineExceededWhenJobTimesOut) {
    BusExecutor executor(std::make_unique<NoopSession>("mock://executor-timeout"));
    ASSERT_TRUE(executor.start().is_ok());

    const Status status = executor.submit(
        "slow-job",
        std::chrono::milliseconds(30),
        [](anolis_provider_ezo::i2c::ISession &) {
            std::this_thread::sleep_for(std::chrono::milliseconds(90));
            return Status::ok();
        });

    EXPECT_EQ(status.code, StatusCode::DeadlineExceeded);
    EXPECT_NE(status.message.find("timed out"), std::string::npos);

    executor.stop();

    const auto metrics = executor.snapshot_metrics();
    EXPECT_EQ(metrics.submitted, 1U);
    EXPECT_EQ(metrics.started, 1U);
    EXPECT_EQ(metrics.timed_out, 1U);
}

TEST(BusExecutorTest, StopCancelsQueuedJobs) {
    BusExecutor executor(std::make_unique<NoopSession>("mock://executor-stop"));
    ASSERT_TRUE(executor.start().is_ok());

    std::promise<void> slow_started;
    std::future<void> slow_started_future = slow_started.get_future();
    std::atomic<bool> queue_submit_called{false};

    Status slow_status;
    Status queued_status;

    std::thread slow_submitter([&]() {
        slow_status = executor.submit(
            "slow-job",
            std::chrono::milliseconds(1000),
            [&slow_started](anolis_provider_ezo::i2c::ISession &) {
                slow_started.set_value();
                std::this_thread::sleep_for(std::chrono::milliseconds(180));
                return Status::ok();
            });
    });

    ASSERT_EQ(slow_started_future.wait_for(std::chrono::milliseconds(200)),
              std::future_status::ready);

    std::thread queued_submitter([&]() {
        queue_submit_called.store(true);
        queued_status = executor.submit(
            "queued-job",
            std::chrono::milliseconds(800),
            [](anolis_provider_ezo::i2c::ISession &) { return Status::ok(); });
    });

    for(int i = 0; i < 50; ++i) {
        if(queue_submit_called.load() && executor.snapshot_metrics().submitted >= 2U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    executor.stop();

    slow_submitter.join();
    queued_submitter.join();

    EXPECT_TRUE(slow_status.is_ok()) << slow_status.message;
    EXPECT_EQ(queued_status.code, StatusCode::Cancelled);

    const auto metrics = executor.snapshot_metrics();
    EXPECT_EQ(metrics.cancelled, 1U);
}

} // namespace
