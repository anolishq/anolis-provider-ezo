#include "core/runtime_state.hpp"

#include <memory>
#include <mutex>
#include <sstream>
#include <utility>

namespace anolis_provider_ezo::runtime {
namespace {

std::mutex g_mutex;
RuntimeState g_state;
std::shared_ptr<i2c::BusExecutor> g_executor;

bool has_prefix(const std::string &value, const std::string &prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::unique_ptr<i2c::ISession> make_session(const ProviderConfig &config) {
    if(has_prefix(config.bus_path, "mock://")) {
        return std::make_unique<i2c::NoopSession>(config.bus_path);
    }

#if defined(__linux__)
    return std::make_unique<i2c::LinuxSession>(
        config.bus_path,
        config.timeout_ms,
        config.retry_count);
#else
    return std::make_unique<i2c::NoopSession>(config.bus_path);
#endif
}

} // namespace

void shutdown() {
    std::shared_ptr<i2c::BusExecutor> executor;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        executor = std::move(g_executor);
        g_state.i2c_executor_running = false;
    }
    if(executor) {
        executor->stop();
    }
}

void reset() {
    shutdown();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state = RuntimeState{};
}

void initialize(const ProviderConfig &config) {
    reset();

    RuntimeState state;
    state.config = config;
    state.started_at = std::chrono::system_clock::now();
    state.ready = false;

    auto executor = std::make_shared<i2c::BusExecutor>(make_session(config));
    const i2c::Status start_status = executor->start();

    state.i2c_executor_running = start_status.is_ok();
    state.i2c_status_message = start_status.message;
    state.i2c_metrics = executor->snapshot_metrics();
    state.ready = start_status.is_ok();

    std::ostringstream startup;
    if(start_status.is_ok()) {
        startup << "phase2 i2c executor ready: devices="
                << state.config.devices.size()
                << ", bus=" << state.config.bus_path;
    } else {
        startup << "phase2 i2c executor unavailable: "
                << start_status.message
                << ", bus=" << state.config.bus_path;
    }
    state.startup_message = startup.str();

    std::lock_guard<std::mutex> lock(g_mutex);
    g_state = std::move(state);
    if(start_status.is_ok()) {
        g_executor = std::move(executor);
    }
}

RuntimeState snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeState copy = g_state;
    if(g_executor) {
        copy.i2c_executor_running = g_executor->is_running();
        copy.i2c_metrics = g_executor->snapshot_metrics();
        if(copy.i2c_metrics.last_error.empty()) {
            copy.i2c_status_message = "ok";
        } else {
            copy.i2c_status_message = copy.i2c_metrics.last_error;
        }
    }
    return copy;
}

i2c::Status submit_i2c_job(const std::string &job_name,
                           std::chrono::milliseconds timeout,
                           i2c::BusExecutor::Job job) {
    std::shared_ptr<i2c::BusExecutor> executor;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        executor = g_executor;
    }

    if(!executor) {
        return i2c::Status{
            i2c::StatusCode::Unavailable,
            "i2c executor is not running",
        };
    }

    return executor->submit(job_name, timeout, std::move(job));
}

} // namespace anolis_provider_ezo::runtime
