#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "config/provider_config.hpp"
#include "i2c/bus_executor.hpp"
#include "protocol.pb.h"

namespace anolis_provider_ezo::runtime {

using CapabilitySet = anolis::deviceprovider::v1::CapabilitySet;
using Device = anolis::deviceprovider::v1::Device;

struct SignalSample {
    bool available = true;
    bool has_value = false;
    double value = 0.0;
    std::string unavailable_reason;
};

struct DeviceSampleCache {
    bool has_sample = false;
    bool last_read_ok = false;
    std::chrono::system_clock::time_point sampled_at{};
    std::string last_error;
    uint64_t success_count = 0;
    uint64_t failure_count = 0;
    uint64_t sequence = 0;
    std::vector<SignalSample> signals;
};

struct ActiveDevice {
    DeviceSpec spec;
    Device descriptor;
    CapabilitySet capabilities;
    std::string startup_product_code;
    std::string startup_firmware_version;
    DeviceSampleCache sample;
};

struct ExcludedDevice {
    DeviceSpec spec;
    std::string reason;
};

struct RuntimeState {
    ProviderConfig config;
    std::vector<ActiveDevice> active_devices;
    std::vector<ExcludedDevice> excluded_devices;
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
i2c::Status refresh_device_sample(const std::string &device_id);

} // namespace anolis_provider_ezo::runtime
