#include "core/health.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

namespace anolis_provider_ezo::health {
namespace {

constexpr int kMinSamplePeriodMs = 50;
constexpr int kMinStaleAfterMs = 500;

int sample_period_ms(const runtime::RuntimeState &state) {
    return std::max(state.config.query_delay_us / 1000, kMinSamplePeriodMs);
}

int stale_after_ms(const runtime::RuntimeState &state) {
    return std::max(sample_period_ms(state) * 3, kMinStaleAfterMs);
}

google::protobuf::Timestamp to_proto_timestamp(
    std::chrono::system_clock::time_point time_point) {
    google::protobuf::Timestamp timestamp;
    const auto epoch = time_point.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch - seconds);
    timestamp.set_seconds(seconds.count());
    timestamp.set_nanos(static_cast<int32_t>(nanos.count()));
    return timestamp;
}

} // namespace

ProviderHealth make_provider_health(const runtime::RuntimeState &state) {
    ProviderHealth health;
    const bool degraded = !state.ready || !state.excluded_devices.empty();
    health.set_state(degraded ? ProviderHealth::STATE_DEGRADED : ProviderHealth::STATE_OK);
    health.set_message(degraded ? "degraded" : "ok");
    (*health.mutable_metrics())["impl"] = "ezo";
    (*health.mutable_metrics())["phase"] = "4";
    (*health.mutable_metrics())["configured_devices"] =
        std::to_string(state.config.devices.size());
    (*health.mutable_metrics())["active_devices"] =
        std::to_string(state.active_devices.size());
    (*health.mutable_metrics())["excluded_devices"] =
        std::to_string(state.excluded_devices.size());
    (*health.mutable_metrics())["bus_path"] = state.config.bus_path;
    (*health.mutable_metrics())["i2c_executor_running"] =
        state.i2c_executor_running ? "true" : "false";
    (*health.mutable_metrics())["i2c_queue_depth"] =
        std::to_string(state.i2c_metrics.queue_depth);
    (*health.mutable_metrics())["i2c_jobs_submitted"] =
        std::to_string(state.i2c_metrics.submitted);
    (*health.mutable_metrics())["i2c_jobs_started"] =
        std::to_string(state.i2c_metrics.started);
    (*health.mutable_metrics())["i2c_jobs_succeeded"] =
        std::to_string(state.i2c_metrics.succeeded);
    (*health.mutable_metrics())["i2c_jobs_failed"] =
        std::to_string(state.i2c_metrics.failed);
    (*health.mutable_metrics())["i2c_jobs_timed_out"] =
        std::to_string(state.i2c_metrics.timed_out);
    (*health.mutable_metrics())["i2c_jobs_cancelled"] =
        std::to_string(state.i2c_metrics.cancelled);
    (*health.mutable_metrics())["i2c_status"] = state.i2c_status_message;
    if(!state.i2c_metrics.last_error.empty()) {
        (*health.mutable_metrics())["i2c_last_error"] = state.i2c_metrics.last_error;
    }
    return health;
}

std::vector<DeviceHealth> make_device_health(const runtime::RuntimeState &state,
                                             bool include_excluded) {
    std::vector<DeviceHealth> devices;
    devices.reserve(state.active_devices.size() +
                    (include_excluded ? state.excluded_devices.size() : 0));

    const auto now = std::chrono::system_clock::now();
    const auto stale_after = stale_after_ms(state);

    for(const auto &device : state.active_devices) {
        DeviceHealth health;
        health.set_device_id(device.spec.id);
        (*health.mutable_metrics())["type"] = to_string(device.spec.type);
        (*health.mutable_metrics())["address"] = format_i2c_address(device.spec.address);
        (*health.mutable_metrics())["sample_success_count"] =
            std::to_string(device.sample.success_count);
        (*health.mutable_metrics())["sample_failure_count"] =
            std::to_string(device.sample.failure_count);
        if(!device.startup_product_code.empty()) {
            (*health.mutable_metrics())["startup_product_code"] = device.startup_product_code;
        }
        if(!device.startup_firmware_version.empty()) {
            (*health.mutable_metrics())["startup_firmware"] = device.startup_firmware_version;
        }

        if(device.sample.has_sample) {
            const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - device.sample.sampled_at);
            (*health.mutable_metrics())["sample_age_ms"] = std::to_string(age_ms.count());
            (*health.mutable_metrics())["sample_stale_after_ms"] = std::to_string(stale_after);
            *health.mutable_last_seen() = to_proto_timestamp(device.sample.sampled_at);

            if(!device.sample.last_read_ok) {
                health.set_state(DeviceHealth::STATE_FAULT);
                health.set_message("latest read failed; cached sample may be stale");
            } else if(age_ms.count() > stale_after) {
                health.set_state(DeviceHealth::STATE_STALE);
                health.set_message("sample is stale");
            } else {
                health.set_state(DeviceHealth::STATE_OK);
                health.set_message("ok");
            }
        } else {
            health.set_state(DeviceHealth::STATE_UNREACHABLE);
            health.set_message("no sample available yet");
        }

        if(!device.sample.last_error.empty()) {
            (*health.mutable_metrics())["last_error"] = device.sample.last_error;
        }

        devices.push_back(std::move(health));
    }

    if(include_excluded) {
        for(const auto &excluded : state.excluded_devices) {
            DeviceHealth health;
            health.set_device_id(excluded.spec.id);
            health.set_state(DeviceHealth::STATE_UNREACHABLE);
            health.set_message(excluded.reason);
            (*health.mutable_metrics())["excluded"] = "true";
            (*health.mutable_metrics())["type"] = to_string(excluded.spec.type);
            (*health.mutable_metrics())["address"] = format_i2c_address(excluded.spec.address);
            devices.push_back(std::move(health));
        }
    }

    return devices;
}

void populate_wait_ready(const runtime::RuntimeState &state, WaitReadyResponse &out) {
    int64_t uptime = 0;
    if(state.started_at.time_since_epoch().count() > 0) {
        const auto now = std::chrono::system_clock::now();
        uptime = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.started_at)
                     .count();
    }

    (*out.mutable_diagnostics())["ready"] = state.ready ? "true" : "false";
    (*out.mutable_diagnostics())["init_time_ms"] = "0";
    (*out.mutable_diagnostics())["uptime_ms"] = std::to_string(uptime);
    (*out.mutable_diagnostics())["configured_device_count"] =
        std::to_string(state.config.devices.size());
    (*out.mutable_diagnostics())["active_device_count"] =
        std::to_string(state.active_devices.size());
    (*out.mutable_diagnostics())["excluded_device_count"] =
        std::to_string(state.excluded_devices.size());
    (*out.mutable_diagnostics())["provider_version"] = ANOLIS_PROVIDER_EZO_VERSION;
    (*out.mutable_diagnostics())["provider_impl"] = "ezo";
    (*out.mutable_diagnostics())["phase"] = "4";
    (*out.mutable_diagnostics())["startup_message"] = state.startup_message;
    (*out.mutable_diagnostics())["bus_path"] = state.config.bus_path;
    (*out.mutable_diagnostics())["i2c_executor_running"] =
        state.i2c_executor_running ? "true" : "false";
    (*out.mutable_diagnostics())["i2c_queue_depth"] =
        std::to_string(state.i2c_metrics.queue_depth);
    (*out.mutable_diagnostics())["i2c_jobs_submitted"] =
        std::to_string(state.i2c_metrics.submitted);
    (*out.mutable_diagnostics())["i2c_jobs_timed_out"] =
        std::to_string(state.i2c_metrics.timed_out);
    (*out.mutable_diagnostics())["i2c_status"] = state.i2c_status_message;
}

} // namespace anolis_provider_ezo::health
