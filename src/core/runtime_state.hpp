#pragma once

#include <chrono>
#include <string>

#include "config/provider_config.hpp"
#include "i2c/bus_executor.hpp"

namespace anolis_provider_ezo::runtime {

struct RuntimeState {
    ProviderConfig config;
    bool ready = false;
    std::string startup_message;
    std::chrono::system_clock::time_point started_at;
    bool i2c_executor_running = false;
    i2c::BusExecutorMetrics i2c_metrics;
    std::string i2c_status_message = "not initialized";
};

void reset();
void initialize(const ProviderConfig &config);
void shutdown();
RuntimeState snapshot();
i2c::Status submit_i2c_job(const std::string &job_name,
                           std::chrono::milliseconds timeout,
                           i2c::BusExecutor::Job job);

} // namespace anolis_provider_ezo::runtime
