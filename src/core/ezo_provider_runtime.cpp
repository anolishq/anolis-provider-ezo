#include "core/ezo_provider_runtime.hpp"

#include <google/protobuf/timestamp.pb.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "anolis/provider_sdk/quality.hpp"
#include "anolis/provider_sdk/result.hpp"
#include "config/provider_config.hpp"
#include "core/runtime_state.hpp"
#include "devices/common/device_adapter.hpp"
#include "devices/common/sample_helpers.hpp"
#include "i2c/ezo_i2c_bridge.hpp"
#include "logging/logger.hpp"
#include "protocol.pb.h"

extern "C" {
#include "ezo_control.h"
#include "ezo_product.h"
}

#ifndef ANOLIS_PROVIDER_EZO_VERSION
#define ANOLIS_PROVIDER_EZO_VERSION "0.0.0"
#endif

namespace anolis_provider_ezo {
namespace {

namespace adpp = anolis::deviceprovider::v1;
namespace sdk = anolis::provider_sdk;
using adpp::FunctionSpec;
using adpp::SignalValue;
using adpp::Status;

constexpr int kMinSamplePeriodMs = 50;
constexpr int kMinStaleAfterMs = 500;

int sample_period_ms(const runtime::RuntimeState& state) {
    return std::max(state.config.query_delay_us / 1000, kMinSamplePeriodMs);
}

int stale_after_ms(const runtime::RuntimeState& state) {
    return std::max(sample_period_ms(state) * 3, kMinStaleAfterMs);
}

bool is_mock_mode(const runtime::RuntimeState& state) { return state.config.bus_path.rfind("mock://", 0) == 0; }

Status::Code map_i2c_status_code(i2c::StatusCode code) {
    switch (code) {
        case i2c::StatusCode::Ok:
            return Status::CODE_OK;
        case i2c::StatusCode::InvalidArgument:
            return Status::CODE_INVALID_ARGUMENT;
        case i2c::StatusCode::NotFound:
            return Status::CODE_NOT_FOUND;
        case i2c::StatusCode::Unavailable:
            return Status::CODE_UNAVAILABLE;
        case i2c::StatusCode::DeadlineExceeded:
            return Status::CODE_DEADLINE_EXCEEDED;
        case i2c::StatusCode::Cancelled:
            return Status::CODE_UNAVAILABLE;
        case i2c::StatusCode::Internal:
            return Status::CODE_INTERNAL;
    }
    return Status::CODE_INTERNAL;
}

google::protobuf::Timestamp to_proto_timestamp(std::chrono::system_clock::time_point time_point) {
    google::protobuf::Timestamp timestamp;
    const auto epoch = time_point.time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch - seconds);
    timestamp.set_seconds(seconds.count());
    timestamp.set_nanos(static_cast<int32_t>(nanos.count()));
    return timestamp;
}

const runtime::ActiveDevice* find_device(const runtime::RuntimeState& state, const std::string& device_id) {
    auto it = std::find_if(state.active_devices.begin(), state.active_devices.end(),
                           [&device_id](const runtime::ActiveDevice& device) { return device.spec.id == device_id; });
    return it == state.active_devices.end() ? nullptr : &(*it);
}

const FunctionSpec* find_function_by_id(const runtime::ActiveDevice& device, uint32_t function_id) {
    for (const auto& function : device.capabilities.functions()) {
        if (function.function_id() == function_id) {
            return &function;
        }
    }
    return nullptr;
}

const FunctionSpec* find_function_by_name(const runtime::ActiveDevice& device, const std::string& function_name) {
    for (const auto& function : device.capabilities.functions()) {
        if (function.name() == function_name) {
            return &function;
        }
    }
    return nullptr;
}

int find_signal_index(const runtime::ActiveDevice& device, const std::string& signal_id) {
    for (int i = 0; i < device.capabilities.signals_size(); ++i) {
        if (device.capabilities.signals(i).signal_id() == signal_id) {
            return i;
        }
    }
    return -1;
}

// The D1 conversion: ezo's cached per-family SignalSample -> a proto SignalValue
// with quality_from-stamped quality + the rich per-signal metadata. Unchanged from
// the pre-migration read path except it now lives here and uses sdk::quality_from
// (identical to ezo's old devices::quality_from).
void populate_signal_value(const runtime::RuntimeState& state, const runtime::ActiveDevice& device, size_t signal_index,
                           SignalValue* value) {
    if (value == nullptr || signal_index >= static_cast<size_t>(device.capabilities.signals_size())) {
        return;
    }

    const auto& signal_spec = device.capabilities.signals(static_cast<int>(signal_index));
    value->set_signal_id(signal_spec.signal_id());
    value->mutable_value()->set_type(adpp::VALUE_TYPE_DOUBLE);
    *value->mutable_timestamp() = to_proto_timestamp(device.sample.sampled_at);

    const runtime::SignalSample* signal_sample = nullptr;
    if (signal_index < device.sample.signals.size()) {
        signal_sample = &device.sample.signals[signal_index];
    }
    if (signal_sample != nullptr && signal_sample->has_value) {
        value->mutable_value()->set_double_value(signal_sample->value);
    }

    const auto now = std::chrono::system_clock::now();
    const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - device.sample.sampled_at);
    const auto stale_ms = stale_after_ms(state);

    SignalValue::Quality quality = SignalValue::QUALITY_UNKNOWN;  // no sample slot -> unknown
    if (signal_sample != nullptr) {
        quality = sdk::quality_from(signal_sample->available, signal_sample->has_value, device.sample.last_read_ok,
                                    age_ms.count(), stale_ms);
    }
    value->set_quality(quality);

    (*value->mutable_metadata())["age_ms"] = std::to_string(age_ms.count());
    (*value->mutable_metadata())["stale_after_ms"] = std::to_string(stale_ms);
    (*value->mutable_metadata())["sample_success_count"] = std::to_string(device.sample.success_count);
    (*value->mutable_metadata())["sample_failure_count"] = std::to_string(device.sample.failure_count);
    if (!device.sample.last_error.empty()) {
        (*value->mutable_metadata())["last_error"] = device.sample.last_error;
    }
    if (signal_sample != nullptr && !signal_sample->available) {
        (*value->mutable_metadata())["unavailable"] = "true";
        (*value->mutable_metadata())["unavailable_reason"] = signal_sample->unavailable_reason;
    }
}

bool validate_call_args(uint32_t function_id, const sdk::ValueMap& args, bool* set_led_enabled,
                        std::string* error_out) {
    auto set_error = [&](const std::string& message) {
        if (error_out != nullptr) {
            *error_out = message;
        }
    };

    if (function_id == runtime::kFunctionFind || function_id == runtime::kFunctionSleep) {
        if (!args.empty()) {
            set_error("function does not accept args");
            return false;
        }
        return true;
    }

    if (function_id == runtime::kFunctionSetLed) {
        const auto it = args.find("enabled");
        if (it == args.end()) {
            set_error("missing required arg 'enabled'");
            return false;
        }
        if (args.size() != 1) {
            set_error("set_led accepts only one arg: enabled");
            return false;
        }
        if (it->second.type() != adpp::VALUE_TYPE_BOOL) {
            set_error("arg 'enabled' must be VALUE_TYPE_BOOL");
            return false;
        }
        if (set_led_enabled != nullptr) {
            *set_led_enabled = it->second.bool_value();
        }
        return true;
    }

    set_error("unsupported function_id");
    return false;
}

// Execute one control call through the serialized I2C executor (the hardware seam
// stays here). Mock mode short-circuits. Returns a neutral AdapterCallResult.
sdk::AdapterCallResult execute_safe_call(const runtime::RuntimeState& state, const runtime::ActiveDevice& device,
                                         uint32_t function_id, bool set_led_enabled,
                                         std::chrono::milliseconds timeout) {
    if (is_mock_mode(state)) {
        return {true, Status::CODE_OK, ""};
    }

    const ezo_product_id_t product_id = devices::adapter_for(device.spec.type).expected_product;
    if (product_id == EZO_PRODUCT_UNKNOWN) {
        return {false, Status::CODE_INTERNAL, "unsupported configured device type for control call"};
    }

    const i2c::Status status = runtime::submit_i2c_job(
        "call:" + std::to_string(function_id) + ":" + device.spec.id, timeout, [&](i2c::ISession& session) {
            i2c::EzoDeviceBinding binding;
            i2c::Status bind_status =
                i2c::bind_ezo_i2c_device(session, static_cast<uint8_t>(device.spec.address), binding);
            if (!bind_status.is_ok()) {
                return bind_status;
            }

            ezo_timing_hint_t hint{};
            ezo_result_t result = EZO_ERR_INVALID_ARGUMENT;
            switch (function_id) {
                case runtime::kFunctionFind:
                    result = ezo_control_send_find_i2c(&binding.device, product_id, &hint);
                    break;
                case runtime::kFunctionSetLed:
                    result =
                        ezo_control_send_led_set_i2c(&binding.device, product_id, set_led_enabled ? 1U : 0U, &hint);
                    break;
                case runtime::kFunctionSleep:
                    result = ezo_control_send_sleep_i2c(&binding.device, product_id, &hint);
                    break;
                default:
                    return devices::make_status(i2c::StatusCode::InvalidArgument, "unsupported function_id");
            }

            if (result != EZO_OK) {
                return devices::status_from_ezo_result(result, "send control command");
            }
            devices::wait_for_timing_hint(hint);
            return i2c::Status::ok();
        });
    return {status.is_ok(), map_i2c_status_code(status.code), status.message};
}

}  // namespace

sdk::ProviderMetadata EzoProviderRuntime::metadata() const {
    sdk::ProviderMetadata meta;
    meta.name = "anolis-provider-ezo";
    meta.version = ANOLIS_PROVIDER_EZO_VERSION;
    meta.protocol_version = "v1";
    // The SDK's handle_hello injects transport + max_frame_bytes; supply only ezo's
    // extra advertisements (preserves the pre-migration Hello metadata).
    meta.hello_extra["supports_wait_ready"] = "true";
    meta.hello_extra["discovery_mode"] = "manual";
    meta.hello_extra["i2c_execution_model"] = "single_executor";
    meta.hello_extra["coverage"] = "all_families";
    return meta;
}

sdk::ReadinessReport EzoProviderRuntime::readiness() const {
    const runtime::RuntimeState state = runtime::snapshot();
    sdk::ReadinessReport r;
    r.ready = state.ready;
    r.configured_device_count = static_cast<int>(state.config.devices.size());
    for (const auto& device : state.active_devices) {
        r.successful_device_ids.push_back(device.spec.id);
    }
    for (const auto& excluded : state.excluded_devices) {
        r.failed_devices.push_back({excluded.spec.id, to_string(excluded.spec.type), excluded.reason});
    }
    r.startup_policy = "strict";  // ezo is strict-partial (excludes failed devices, serves the rest)
    r.provider_impl = "ezo";

    // Restore ezo's pre-migration WaitReady diagnostics surface (thinned by the
    // SDK migration). The SDK's handle_wait_ready already emits device_count,
    // startup_*, provider_version and provider_impl from the structured report;
    // these are the ezo-specific extras it merges verbatim. (anolis-provider-ezo#88)
    const auto ready_point = state.ready ? state.ready_at : std::chrono::system_clock::now();
    const auto init_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ready_point - state.started_at).count();
    int64_t uptime_ms = 0;
    if (state.started_at.time_since_epoch().count() > 0) {
        const auto now = std::chrono::system_clock::now();
        uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.started_at).count();
    }
    uint64_t call_success_total = 0;
    uint64_t call_failure_total = 0;
    for (const auto& device : state.active_devices) {
        call_success_total += device.call_success_count;
        call_failure_total += device.call_failure_count;
    }

    auto& diag = r.extra_diagnostics;
    diag["init_time_ms"] = std::to_string(init_ms < 0 ? 0 : init_ms);
    diag["ready"] = state.ready ? "true" : "false";
    diag["uptime_ms"] = std::to_string(uptime_ms);
    diag["configured_device_count"] = std::to_string(state.config.devices.size());
    diag["active_device_count"] = std::to_string(state.active_devices.size());
    diag["excluded_device_count"] = std::to_string(state.excluded_devices.size());
    diag["call_success_total"] = std::to_string(call_success_total);
    diag["call_failure_total"] = std::to_string(call_failure_total);
    diag["startup_message"] = state.startup_message;
    diag["bus_path"] = state.config.bus_path;
    diag["i2c_executor_running"] = state.i2c_executor_running ? "true" : "false";
    diag["i2c_queue_depth"] = std::to_string(state.i2c_metrics.queue_depth);
    diag["i2c_jobs_submitted"] = std::to_string(state.i2c_metrics.submitted);
    diag["i2c_jobs_timed_out"] = std::to_string(state.i2c_metrics.timed_out);
    diag["i2c_status"] = state.i2c_status_message;
    return r;
}

sdk::DeviceHealthExtra EzoProviderRuntime::device_health(const std::string& device_id) const {
    // Restore ezo's pre-migration per-device metrics (SDK#9) from ONE snapshot, so
    // last_seen and the age metrics are a single atomic view. The per-device STATE
    // nuance (STALE/FAULT) is intentionally NOT restored here: the SDK derives
    // device state from readiness() (active -> OK, excluded -> UNREACHABLE); a
    // per-device device_state hook is a separate deferred follow-up.
    const runtime::RuntimeState state = runtime::snapshot();
    sdk::DeviceHealthExtra extra;
    auto& m = extra.metrics;

    if (const runtime::ActiveDevice* device = find_device(state, device_id)) {
        m["type"] = to_string(device->spec.type);
        m["address"] = format_i2c_address(device->spec.address);
        m["sample_success_count"] = std::to_string(device->sample.success_count);
        m["sample_failure_count"] = std::to_string(device->sample.failure_count);
        m["call_success_count"] = std::to_string(device->call_success_count);
        m["call_failure_count"] = std::to_string(device->call_failure_count);
        // Transport-level io counters (ezo#100): bus/electrical health only —
        // protocol-level failures (garbage payloads, EZO not-ready) surface in
        // the sample_*/call_* counters above. Shared key vocabulary with other
        // providers; magnitudes count raw I2C transactions, not logical ops.
        const i2c::IoStats io = runtime::io_stats_for(static_cast<uint8_t>(device->spec.address));
        m["io_ok"] = std::to_string(io.ok);
        m["io_failed"] = std::to_string(io.failed);
        m["io_retried_attempts"] = std::to_string(io.retried_attempts);
        if (!device->startup_product_code.empty()) {
            m["startup_product_code"] = device->startup_product_code;
        }
        if (!device->startup_firmware_version.empty()) {
            m["startup_firmware"] = device->startup_firmware_version;
        }
        const auto now = std::chrono::system_clock::now();
        if (device->sample.has_sample) {
            const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - device->sample.sampled_at);
            m["sample_age_ms"] = std::to_string(age_ms.count());
            m["sample_stale_after_ms"] = std::to_string(stale_after_ms(state));
            extra.last_seen = to_proto_timestamp(device->sample.sampled_at);
        }
        if (device->has_last_call) {
            m["last_call_function"] = device->last_call_function;
            m["last_call_ok"] = device->last_call_ok ? "true" : "false";
            const auto last_call_age_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - device->last_call_at);
            m["last_call_age_ms"] = std::to_string(last_call_age_ms.count());
            if (!device->last_call_error.empty()) {
                m["last_call_error"] = device->last_call_error;
            }
        }
        if (!device->sample.last_error.empty()) {
            m["last_error"] = device->sample.last_error;
        }
        return extra;
    }

    // Excluded devices surface on get_health (readiness() maps them to
    // failed_devices); restore their startup-identity metrics.
    for (const auto& excluded : state.excluded_devices) {
        if (excluded.spec.id == device_id) {
            m["excluded"] = "true";
            m["type"] = to_string(excluded.spec.type);
            m["address"] = format_i2c_address(excluded.spec.address);
            // Excluded devices are where transport counters are most
            // diagnostic: the startup probe that caused the exclusion already
            // accumulated io stats (bus NAKs vs identity mismatch).
            const i2c::IoStats io = runtime::io_stats_for(static_cast<uint8_t>(excluded.spec.address));
            m["io_ok"] = std::to_string(io.ok);
            m["io_failed"] = std::to_string(io.failed);
            m["io_retried_attempts"] = std::to_string(io.retried_attempts);
            break;
        }
    }
    return extra;
}

std::vector<std::string> EzoProviderRuntime::list_device_ids() const {
    const runtime::RuntimeState state = runtime::snapshot();
    std::vector<std::string> ids;
    ids.reserve(state.active_devices.size());
    for (const auto& device : state.active_devices) {
        ids.push_back(device.spec.id);
    }
    return ids;
}

bool EzoProviderRuntime::has_device(const std::string& device_id) const {
    const runtime::RuntimeState state = runtime::snapshot();
    return find_device(state, device_id) != nullptr;
}

adpp::Device EzoProviderRuntime::device_info(const std::string& device_id) const {
    const runtime::RuntimeState state = runtime::snapshot();
    const runtime::ActiveDevice* device = find_device(state, device_id);
    return device == nullptr ? adpp::Device{} : device->descriptor;
}

adpp::CapabilitySet EzoProviderRuntime::capabilities(const std::string& device_id) const {
    const runtime::RuntimeState state = runtime::snapshot();
    const runtime::ActiveDevice* device = find_device(state, device_id);
    return device == nullptr ? adpp::CapabilitySet{} : device->capabilities;
}

sdk::AdapterReadResult EzoProviderRuntime::read(const std::string& device_id,
                                                const std::vector<std::string>& signal_ids) {
    runtime::RuntimeState state = runtime::snapshot();
    const runtime::ActiveDevice* device = find_device(state, device_id);
    if (device == nullptr) {
        return {false, Status::CODE_NOT_FOUND, "unknown device_id", {}};
    }

    // [§7.2] An empty signal_ids returns the curated default set (the SDK passes
    // the empty vector straight through; the provider owns the expansion).
    std::vector<std::string> requested_signal_ids;
    if (signal_ids.empty()) {
        requested_signal_ids = device->default_signal_ids;
    } else {
        requested_signal_ids = signal_ids;
    }

    std::vector<size_t> requested_signal_indexes;
    requested_signal_indexes.reserve(requested_signal_ids.size());
    for (const std::string& signal_id : requested_signal_ids) {
        const int index = find_signal_index(*device, signal_id);
        if (index < 0) {
            return {false, Status::CODE_NOT_FOUND, "unknown signal_id '" + signal_id + "'", {}};
        }
        requested_signal_indexes.push_back(static_cast<size_t>(index));
    }

    // Age-based refresh only (the SDK applies the caller's min_timestamp post-hoc
    // via apply_min_timestamp -> QUALITY_STALE; ezo no longer proactively re-reads
    // hardware to chase a caller's freshness hint).
    bool needs_refresh = !device->sample.has_sample;
    if (!needs_refresh) {
        const auto now = std::chrono::system_clock::now();
        const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - device->sample.sampled_at);
        if (age_ms.count() > sample_period_ms(state)) {
            needs_refresh = true;
        }
    }

    if (needs_refresh) {
        const i2c::Status refresh_status = runtime::refresh_device_sample(device_id);
        state = runtime::snapshot();
        device = find_device(state, device_id);
        if (!refresh_status.is_ok()) {
            logging::warning("read_signals refresh failed for '" + device_id + "': " + refresh_status.message);
            if (device == nullptr || !device->sample.has_sample) {
                return {false, map_i2c_status_code(refresh_status.code), refresh_status.message, {}};
            }
        } else if (device == nullptr) {
            return {false, Status::CODE_NOT_FOUND, "unknown device_id", {}};
        }
    }

    if (!device->sample.has_sample) {
        return {false, Status::CODE_UNAVAILABLE, "no sample available", {}};
    }

    sdk::AdapterReadResult result;
    result.ok = true;
    result.error_code = Status::CODE_OK;
    result.values.reserve(requested_signal_indexes.size());
    for (const size_t signal_index : requested_signal_indexes) {
        populate_signal_value(state, *device, signal_index, &result.values.emplace_back());
    }
    return result;
}

sdk::AdapterCallResult EzoProviderRuntime::call(const std::string& device_id, uint32_t function_id,
                                                const sdk::ValueMap& args) {
    const runtime::RuntimeState state = runtime::snapshot();
    const runtime::ActiveDevice* device = find_device(state, device_id);
    if (device == nullptr) {
        return sdk::call_not_found("unknown device_id");
    }

    const FunctionSpec* function_spec = find_function_by_id(*device, function_id);
    if (function_spec == nullptr) {
        return sdk::call_not_found("unknown function_id");
    }

    bool set_led_enabled = false;
    std::string arg_error;
    if (!validate_call_args(function_id, args, &set_led_enabled, &arg_error)) {
        runtime::record_call_result(device_id, function_spec->name(), false, arg_error);
        return sdk::call_invalid_argument(arg_error);
    }

    // The SDK call() carries no caller deadline; use the provider's configured
    // timeout budget only (the pre-migration CallRequest.deadline clamp is dropped).
    const std::chrono::milliseconds timeout(std::max(state.config.timeout_ms, 1));

    const sdk::AdapterCallResult result = execute_safe_call(state, *device, function_id, set_led_enabled, timeout);
    if (!result.ok) {
        runtime::record_call_result(device_id, function_spec->name(), false, result.error_message);
        logging::warning("call failed for device '" + device_id + "' function '" + function_spec->name() +
                         "': " + result.error_message);
        return result;
    }

    runtime::record_call_result(device_id, function_spec->name(), true, "");
    return result;
}

std::optional<uint32_t> EzoProviderRuntime::resolve_function_id(const std::string& device_id,
                                                                const std::string& function_name) const {
    const runtime::RuntimeState state = runtime::snapshot();
    const runtime::ActiveDevice* device = find_device(state, device_id);
    if (device == nullptr) {
        return std::nullopt;
    }
    const FunctionSpec* function_spec = find_function_by_name(*device, function_name);
    if (function_spec == nullptr) {
        return std::nullopt;
    }
    return function_spec->function_id();
}

}  // namespace anolis_provider_ezo
