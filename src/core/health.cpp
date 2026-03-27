#include "core/health.hpp"

#include <chrono>
#include <string>

namespace anolis_provider_ezo::health {

ProviderHealth make_provider_health(const runtime::RuntimeState &state) {
    ProviderHealth health;
    health.set_state(state.ready ? ProviderHealth::STATE_OK : ProviderHealth::STATE_DEGRADED);
    health.set_message(state.ready ? "ok" : "not ready");
    (*health.mutable_metrics())["impl"] = "ezo";
    (*health.mutable_metrics())["phase"] = "2";
    (*health.mutable_metrics())["configured_devices"] = std::to_string(state.config.devices.size());
    (*health.mutable_metrics())["bus_path"] = state.config.bus_path;
    (*health.mutable_metrics())["i2c_executor_running"] = state.i2c_executor_running ? "true" : "false";
    (*health.mutable_metrics())["i2c_queue_depth"] = std::to_string(state.i2c_metrics.queue_depth);
    (*health.mutable_metrics())["i2c_jobs_submitted"] = std::to_string(state.i2c_metrics.submitted);
    (*health.mutable_metrics())["i2c_jobs_started"] = std::to_string(state.i2c_metrics.started);
    (*health.mutable_metrics())["i2c_jobs_succeeded"] = std::to_string(state.i2c_metrics.succeeded);
    (*health.mutable_metrics())["i2c_jobs_failed"] = std::to_string(state.i2c_metrics.failed);
    (*health.mutable_metrics())["i2c_jobs_timed_out"] = std::to_string(state.i2c_metrics.timed_out);
    (*health.mutable_metrics())["i2c_jobs_cancelled"] = std::to_string(state.i2c_metrics.cancelled);
    (*health.mutable_metrics())["i2c_status"] = state.i2c_status_message;
    if(!state.i2c_metrics.last_error.empty()) {
        (*health.mutable_metrics())["i2c_last_error"] = state.i2c_metrics.last_error;
    }
    return health;
}

std::vector<DeviceHealth> make_device_health(const runtime::RuntimeState &) {
    return {};
}

void populate_wait_ready(const runtime::RuntimeState &state, WaitReadyResponse &out) {
    int64_t uptime = 0;
    if(state.started_at.time_since_epoch().count() > 0) {
        const auto now = std::chrono::system_clock::now();
        uptime = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.started_at).count();
    }

    (*out.mutable_diagnostics())["ready"] = state.ready ? "true" : "false";
    (*out.mutable_diagnostics())["init_time_ms"] = "0";
    (*out.mutable_diagnostics())["uptime_ms"] = std::to_string(uptime);
    (*out.mutable_diagnostics())["configured_device_count"] = std::to_string(state.config.devices.size());
    (*out.mutable_diagnostics())["provider_version"] = ANOLIS_PROVIDER_EZO_VERSION;
    (*out.mutable_diagnostics())["provider_impl"] = "ezo";
    (*out.mutable_diagnostics())["phase"] = "2";
    (*out.mutable_diagnostics())["startup_message"] = state.startup_message;
    (*out.mutable_diagnostics())["bus_path"] = state.config.bus_path;
    (*out.mutable_diagnostics())["i2c_executor_running"] = state.i2c_executor_running ? "true" : "false";
    (*out.mutable_diagnostics())["i2c_queue_depth"] = std::to_string(state.i2c_metrics.queue_depth);
    (*out.mutable_diagnostics())["i2c_jobs_submitted"] = std::to_string(state.i2c_metrics.submitted);
    (*out.mutable_diagnostics())["i2c_jobs_timed_out"] = std::to_string(state.i2c_metrics.timed_out);
    (*out.mutable_diagnostics())["i2c_status"] = state.i2c_status_message;
}

} // namespace anolis_provider_ezo::health
