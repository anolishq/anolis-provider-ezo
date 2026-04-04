/**
 * @file handlers.cpp
 * @brief Implementation of ADPP handlers for the EZO provider.
 *
 * Handler behavior is intentionally conservative: reads may refresh cached
 * samples on demand, while control calls are limited to the small safe function
 * surface and always execute through the serialized I2C executor.
 */

#include "core/handlers.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <google/protobuf/timestamp.pb.h>

#include "core/health.hpp"
#include "core/runtime_state.hpp"
#include "core/transport/framed_stdio.hpp"
#include "i2c/ezo_i2c_bridge.hpp"
#include "logging/logger.hpp"

extern "C" {
#include "ezo_control.h"
#include "ezo_product.h"
}

namespace anolis_provider_ezo::handlers {
namespace {

using FunctionSpec = anolis::deviceprovider::v1::FunctionSpec;
using SignalValue = anolis::deviceprovider::v1::SignalValue;
using Status = anolis::deviceprovider::v1::Status;
using Value = anolis::deviceprovider::v1::Value;
using ValueMap = google::protobuf::Map<std::string, Value>;

constexpr int kMinSamplePeriodMs = 50;
constexpr int kMinStaleAfterMs = 500;

void set_status_ok(Response &response) {
    response.mutable_status()->set_code(Status::CODE_OK);
    response.mutable_status()->set_message("ok");
}

void set_status(Response &response, Status::Code code, const std::string &message) {
    response.mutable_status()->set_code(code);
    response.mutable_status()->set_message(message);
}

int sample_period_ms(const runtime::RuntimeState &state) {
    return std::max(state.config.query_delay_us / 1000, kMinSamplePeriodMs);
}

int stale_after_ms(const runtime::RuntimeState &state) {
    return std::max(sample_period_ms(state) * 3, kMinStaleAfterMs);
}

Status::Code map_i2c_status_code(i2c::StatusCode code) {
    switch(code) {
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

std::chrono::system_clock::time_point from_proto_timestamp(
    const google::protobuf::Timestamp &timestamp) {
    const auto seconds = std::chrono::seconds(timestamp.seconds());
    const auto nanos = std::chrono::nanoseconds(timestamp.nanos());
    const auto duration = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        seconds + nanos);
    return std::chrono::system_clock::time_point(duration);
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

bool has_prefix(const std::string &value, const std::string &prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool is_mock_mode(const runtime::RuntimeState &state) {
    return has_prefix(state.config.bus_path, "mock://");
}

const runtime::ActiveDevice *find_device(const runtime::RuntimeState &state,
                                         const std::string &device_id) {
    auto it = std::find_if(
        state.active_devices.begin(),
        state.active_devices.end(),
        [&device_id](const runtime::ActiveDevice &device) {
            return device.spec.id == device_id;
        });

    if(it == state.active_devices.end()) {
        return nullptr;
    }
    return &(*it);
}

int find_signal_index(const runtime::ActiveDevice &device, const std::string &signal_id) {
    for(int i = 0; i < device.capabilities.signals_size(); ++i) {
        if(device.capabilities.signals(i).signal_id() == signal_id) {
            return i;
        }
    }
    return -1;
}

const FunctionSpec *find_function_by_id(const runtime::ActiveDevice &device, uint32_t function_id) {
    for(const auto &function : device.capabilities.functions()) {
        if(function.function_id() == function_id) {
            return &function;
        }
    }
    return nullptr;
}

const FunctionSpec *find_function_by_name(const runtime::ActiveDevice &device,
                                          const std::string &function_name) {
    for(const auto &function : device.capabilities.functions()) {
        if(function.name() == function_name) {
            return &function;
        }
    }
    return nullptr;
}

i2c::Status make_i2c_status(i2c::StatusCode code, const std::string &message) {
    return i2c::Status{code, message};
}

ezo_product_id_t expected_product_for_type(EzoDeviceType type) {
    switch(type) {
    case EzoDeviceType::Ph:
        return EZO_PRODUCT_PH;
    case EzoDeviceType::Orp:
        return EZO_PRODUCT_ORP;
    case EzoDeviceType::Ec:
        return EZO_PRODUCT_EC;
    case EzoDeviceType::Do:
        return EZO_PRODUCT_DO;
    case EzoDeviceType::Rtd:
        return EZO_PRODUCT_RTD;
    case EzoDeviceType::Hum:
        return EZO_PRODUCT_HUM;
    }

    return EZO_PRODUCT_UNKNOWN;
}

void wait_for_timing_hint(const ezo_timing_hint_t &hint) {
    if(hint.wait_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(hint.wait_ms));
    }
}

i2c::Status status_from_ezo_result(ezo_result_t result, const std::string &context) {
    if(result == EZO_OK) {
        return i2c::Status::ok();
    }

    switch(result) {
    case EZO_ERR_INVALID_ARGUMENT:
        return make_i2c_status(i2c::StatusCode::InvalidArgument,
                               context + ": " + ezo_result_name(result));
    case EZO_ERR_TRANSPORT:
        return make_i2c_status(i2c::StatusCode::Unavailable,
                               context + ": " + ezo_result_name(result));
    case EZO_ERR_BUFFER_TOO_SMALL:
    case EZO_ERR_PROTOCOL:
    case EZO_ERR_PARSE:
        return make_i2c_status(i2c::StatusCode::Internal,
                               context + ": " + ezo_result_name(result));
    case EZO_OK:
        break;
    }

    return make_i2c_status(i2c::StatusCode::Internal,
                           context + ": " + ezo_result_name(result));
}

bool validate_call_args(uint32_t function_id,
                        const ValueMap &args,
                        bool *set_led_enabled,
                        std::string *error_out) {
    auto set_error = [&](const std::string &message) {
        if(error_out != nullptr) {
            *error_out = message;
        }
    };

    if(function_id == runtime::kFunctionFind || function_id == runtime::kFunctionSleep) {
        if(!args.empty()) {
            set_error("function does not accept args");
            return false;
        }
        return true;
    }

    if(function_id == runtime::kFunctionSetLed) {
        const auto it = args.find("enabled");
        if(it == args.end()) {
            set_error("missing required arg 'enabled'");
            return false;
        }
        if(args.size() != 1) {
            set_error("set_led accepts only one arg: enabled");
            return false;
        }
        if(it->second.type() != anolis::deviceprovider::v1::VALUE_TYPE_BOOL) {
            set_error("arg 'enabled' must be VALUE_TYPE_BOOL");
            return false;
        }

        if(set_led_enabled != nullptr) {
            *set_led_enabled = it->second.bool_value();
        }
        return true;
    }

    set_error("unsupported function_id");
    return false;
}

bool resolve_call_timeout(const CallRequest &request,
                         int default_timeout_ms,
                         std::chrono::milliseconds *timeout_out,
                         std::string *error_out) {
    if(timeout_out == nullptr) {
        if(error_out != nullptr) {
            *error_out = "timeout output pointer is null";
        }
        return false;
    }

    const int clamped_default_timeout = std::max(default_timeout_ms, 1);
    std::chrono::milliseconds timeout(clamped_default_timeout);

    // Honor the caller deadline when present, but never increase beyond the
    // provider's local default timeout budget.
    if(request.has_deadline()) {
        const auto deadline = from_proto_timestamp(request.deadline());
        const auto now = std::chrono::system_clock::now();
        if(deadline <= now) {
            if(error_out != nullptr) {
                *error_out = "call deadline already exceeded";
            }
            return false;
        }

        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if(remaining.count() < 1) {
            remaining = std::chrono::milliseconds(1);
        }
        if(remaining < timeout) {
            timeout = remaining;
        }
    }

    *timeout_out = timeout;
    return true;
}

i2c::Status execute_safe_call(const runtime::RuntimeState &state,
                              const runtime::ActiveDevice &device,
                              uint32_t function_id,
                              bool set_led_enabled,
                              std::chrono::milliseconds timeout) {
    if(is_mock_mode(state)) {
        return i2c::Status::ok();
    }

    const ezo_product_id_t product_id = expected_product_for_type(device.spec.type);
    if(product_id == EZO_PRODUCT_UNKNOWN) {
        return make_i2c_status(i2c::StatusCode::Internal,
                               "unsupported configured device type for control call");
    }

    // All control operations traverse the same executor used for reads and
    // startup probes so safe calls cannot interleave with other bus traffic.
    return runtime::submit_i2c_job(
        "call:" + std::to_string(function_id) + ":" + device.spec.id,
        timeout,
        [&](i2c::ISession &session) {
            i2c::EzoDeviceBinding binding;
            i2c::Status bind_status = i2c::bind_ezo_i2c_device(
                session,
                static_cast<uint8_t>(device.spec.address),
                binding);
            if(!bind_status.is_ok()) {
                return bind_status;
            }

            ezo_timing_hint_t hint{};
            ezo_result_t result = EZO_ERR_INVALID_ARGUMENT;
            switch(function_id) {
            case runtime::kFunctionFind:
                result = ezo_control_send_find_i2c(&binding.device, product_id, &hint);
                break;
            case runtime::kFunctionSetLed:
                result = ezo_control_send_led_set_i2c(
                    &binding.device,
                    product_id,
                    set_led_enabled ? 1U : 0U,
                    &hint);
                break;
            case runtime::kFunctionSleep:
                result = ezo_control_send_sleep_i2c(&binding.device, product_id, &hint);
                break;
            default:
                return make_i2c_status(i2c::StatusCode::InvalidArgument,
                                       "unsupported function_id");
            }

            if(result != EZO_OK) {
                return status_from_ezo_result(result, "send control command");
            }

            wait_for_timing_hint(hint);
            return i2c::Status::ok();
        });
}

Value make_bool_value(bool value) {
    Value out;
    out.set_type(anolis::deviceprovider::v1::VALUE_TYPE_BOOL);
    out.set_bool_value(value);
    return out;
}

void populate_signal_value(const runtime::RuntimeState &state,
                           const runtime::ActiveDevice &device,
                           size_t signal_index,
                           SignalValue *value) {
    if(value == nullptr) {
        return;
    }

    if(signal_index >= static_cast<size_t>(device.capabilities.signals_size())) {
        return;
    }

    const auto &signal_spec = device.capabilities.signals(static_cast<int>(signal_index));
    value->set_signal_id(signal_spec.signal_id());
    value->mutable_value()->set_type(anolis::deviceprovider::v1::VALUE_TYPE_DOUBLE);
    *value->mutable_timestamp() = to_proto_timestamp(device.sample.sampled_at);

    const runtime::SignalSample *signal_sample = nullptr;
    if(signal_index < device.sample.signals.size()) {
        signal_sample = &device.sample.signals[signal_index];
    }
    if(signal_sample != nullptr && signal_sample->has_value) {
        value->mutable_value()->set_double_value(signal_sample->value);
    }

    const auto now = std::chrono::system_clock::now();
    const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - device.sample.sampled_at);
    const auto stale_ms = stale_after_ms(state);

    SignalValue::Quality quality = SignalValue::QUALITY_UNKNOWN;
    if(signal_sample == nullptr) {
        quality = SignalValue::QUALITY_UNKNOWN;
    } else if(!signal_sample->available) {
        quality = SignalValue::QUALITY_UNKNOWN;
    } else if(!device.sample.last_read_ok) {
        quality = SignalValue::QUALITY_FAULT;
    } else if(age_ms.count() > stale_ms) {
        quality = SignalValue::QUALITY_STALE;
    } else if(signal_sample->has_value) {
        quality = SignalValue::QUALITY_OK;
    }
    value->set_quality(quality);

    (*value->mutable_metadata())["age_ms"] = std::to_string(age_ms.count());
    (*value->mutable_metadata())["stale_after_ms"] = std::to_string(stale_ms);
    (*value->mutable_metadata())["sample_success_count"] =
        std::to_string(device.sample.success_count);
    (*value->mutable_metadata())["sample_failure_count"] =
        std::to_string(device.sample.failure_count);
    if(!device.sample.last_error.empty()) {
        (*value->mutable_metadata())["last_error"] = device.sample.last_error;
    }
    if(signal_sample != nullptr && !signal_sample->available) {
        (*value->mutable_metadata())["unavailable"] = "true";
        (*value->mutable_metadata())["unavailable_reason"] = signal_sample->unavailable_reason;
    }
}

} // namespace

void handle_hello(const HelloRequest &request, Response &response) {
    if(request.protocol_version() != "v1") {
        set_status(response, Status::CODE_FAILED_PRECONDITION,
                   "unsupported protocol_version; expected v1");
        return;
    }

    auto *hello = response.mutable_hello();
    hello->set_protocol_version("v1");
    hello->set_provider_name("anolis-provider-ezo");
    hello->set_provider_version(ANOLIS_PROVIDER_EZO_VERSION);
    (*hello->mutable_metadata())["transport"] = "stdio+uint32_le";
    (*hello->mutable_metadata())["max_frame_bytes"] = std::to_string(transport::kMaxFrameBytes);
    (*hello->mutable_metadata())["supports_wait_ready"] = "true";
    (*hello->mutable_metadata())["discovery_mode"] = "manual";
    (*hello->mutable_metadata())["i2c_execution_model"] = "single_executor";
    (*hello->mutable_metadata())["coverage"] = "all_families";
    set_status_ok(response);
}

void handle_wait_ready(const WaitReadyRequest &, Response &response) {
    auto *out = response.mutable_wait_ready();
    health::populate_wait_ready(runtime::snapshot(), *out);
    set_status_ok(response);
}

void handle_list_devices(const ListDevicesRequest &request, Response &response) {
    const runtime::RuntimeState state = runtime::snapshot();
    auto *out = response.mutable_list_devices();
    for(const auto &device : state.active_devices) {
        *out->add_devices() = device.descriptor;
    }
    if(request.include_health()) {
        for(const auto &entry : health::make_device_health(state, false)) {
            *out->add_device_health() = entry;
        }
    }
    set_status_ok(response);
}

void handle_describe_device(const DescribeDeviceRequest &request, Response &response) {
    if(request.device_id().empty()) {
        set_status(response, Status::CODE_INVALID_ARGUMENT, "device_id is required");
        return;
    }

    const runtime::RuntimeState state = runtime::snapshot();
    const runtime::ActiveDevice *device = find_device(state, request.device_id());
    if(device == nullptr) {
        set_status(response, Status::CODE_NOT_FOUND, "unknown device_id");
        return;
    }

    auto *out = response.mutable_describe_device();
    *out->mutable_device() = device->descriptor;
    *out->mutable_capabilities() = device->capabilities;
    set_status_ok(response);
}

void handle_read_signals(const ReadSignalsRequest &request, Response &response) {
    if(request.device_id().empty()) {
        set_status(response, Status::CODE_INVALID_ARGUMENT, "device_id is required");
        return;
    }

    runtime::RuntimeState state = runtime::snapshot();
    const runtime::ActiveDevice *device = find_device(state, request.device_id());
    if(device == nullptr) {
        set_status(response, Status::CODE_NOT_FOUND, "unknown device_id");
        return;
    }

    std::vector<std::string> requested_signal_ids;
    std::vector<size_t> requested_signal_indexes;
    if(request.signal_ids_size() == 0) {
        for(const auto &signal : device->capabilities.signals()) {
            requested_signal_ids.push_back(signal.signal_id());
        }
    } else {
        requested_signal_ids.assign(
            request.signal_ids().begin(),
            request.signal_ids().end());
    }

    for(const std::string &signal_id : requested_signal_ids) {
        const int index = find_signal_index(*device, signal_id);
        if(index < 0) {
            set_status(response, Status::CODE_NOT_FOUND,
                       "unknown signal_id '" + signal_id + "'");
            return;
        }
        requested_signal_indexes.push_back(static_cast<size_t>(index));
    }

    bool needs_refresh = !device->sample.has_sample;
    if(!needs_refresh) {
        const auto now = std::chrono::system_clock::now();
        const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - device->sample.sampled_at);
        if(age_ms.count() > sample_period_ms(state)) {
            needs_refresh = true;
        }
    }

    bool has_min_timestamp = false;
    std::chrono::system_clock::time_point min_timestamp{};
    if(request.has_min_timestamp()) {
        has_min_timestamp = true;
        min_timestamp = from_proto_timestamp(request.min_timestamp());
        if(!device->sample.has_sample || device->sample.sampled_at < min_timestamp) {
            needs_refresh = true;
        }
    }

    if(needs_refresh) {
        const i2c::Status refresh_status = runtime::refresh_device_sample(request.device_id());
        if(!refresh_status.is_ok()) {
            logging::warning("read_signals refresh failed for '" + request.device_id() +
                             "': " + refresh_status.message);
            state = runtime::snapshot();
            device = find_device(state, request.device_id());
            if(device == nullptr || !device->sample.has_sample) {
                set_status(response,
                           map_i2c_status_code(refresh_status.code),
                           refresh_status.message);
                return;
            }
        } else {
            state = runtime::snapshot();
            device = find_device(state, request.device_id());
            if(device == nullptr) {
                set_status(response, Status::CODE_NOT_FOUND, "unknown device_id");
                return;
            }
        }
    }

    if(!device->sample.has_sample) {
        set_status(response, Status::CODE_UNAVAILABLE, "no sample available");
        return;
    }
    if(has_min_timestamp && device->sample.sampled_at < min_timestamp) {
        set_status(response,
                   Status::CODE_DEADLINE_EXCEEDED,
                   "no sample available at or newer than min_timestamp");
        return;
    }

    auto *out = response.mutable_read_signals();
    out->set_device_id(request.device_id());
    for(const size_t signal_index : requested_signal_indexes) {
        populate_signal_value(state, *device, signal_index, out->add_values());
    }

    set_status_ok(response);
}

void handle_call(const CallRequest &request, Response &response) {
    if(request.device_id().empty()) {
        set_status(response, Status::CODE_INVALID_ARGUMENT, "device_id is required");
        return;
    }
    if(request.function_id() == 0 && request.function_name().empty()) {
        set_status(response,
                   Status::CODE_INVALID_ARGUMENT,
                   "function_id or function_name is required");
        return;
    }

    const runtime::RuntimeState state = runtime::snapshot();
    const runtime::ActiveDevice *device = find_device(state, request.device_id());
    if(device == nullptr) {
        set_status(response, Status::CODE_NOT_FOUND, "unknown device_id");
        return;
    }

    const FunctionSpec *by_id = nullptr;
    const FunctionSpec *by_name = nullptr;
    if(request.function_id() != 0) {
        by_id = find_function_by_id(*device, request.function_id());
        if(by_id == nullptr) {
            set_status(response, Status::CODE_NOT_FOUND, "unknown function_id");
            return;
        }
    }
    if(!request.function_name().empty()) {
        by_name = find_function_by_name(*device, request.function_name());
        if(by_name == nullptr) {
            set_status(response, Status::CODE_NOT_FOUND, "unknown function_name");
            return;
        }
    }
    if(by_id != nullptr && by_name != nullptr && by_id->function_id() != by_name->function_id()) {
        set_status(response,
                   Status::CODE_INVALID_ARGUMENT,
                   "function_id does not match function_name");
        return;
    }

    const FunctionSpec *function_spec = by_id != nullptr ? by_id : by_name;
    if(function_spec == nullptr) {
        set_status(response, Status::CODE_NOT_FOUND, "unknown function_id or function_name");
        return;
    }

    bool set_led_enabled = false;
    std::string arg_error;
    if(!validate_call_args(function_spec->function_id(), request.args(), &set_led_enabled, &arg_error)) {
        runtime::record_call_result(request.device_id(), function_spec->name(), false, arg_error);
        set_status(response, Status::CODE_INVALID_ARGUMENT, arg_error);
        return;
    }

    std::chrono::milliseconds timeout{};
    std::string timeout_error;
    if(!resolve_call_timeout(request, state.config.timeout_ms, &timeout, &timeout_error)) {
        runtime::record_call_result(request.device_id(), function_spec->name(), false, timeout_error);
        set_status(response, Status::CODE_DEADLINE_EXCEEDED, timeout_error);
        return;
    }

    const i2c::Status call_status = execute_safe_call(
        state,
        *device,
        function_spec->function_id(),
        set_led_enabled,
        timeout);
    if(!call_status.is_ok()) {
        runtime::record_call_result(
            request.device_id(),
            function_spec->name(),
            false,
            call_status.message);
        logging::warning("call failed for device '" + request.device_id() + "' function '" +
                         function_spec->name() + "': " + call_status.message);
        set_status(response, map_i2c_status_code(call_status.code), call_status.message);
        return;
    }

    auto *out = response.mutable_call();
    out->set_device_id(request.device_id());
    (*out->mutable_results())["accepted"] = make_bool_value(true);
    if(function_spec->function_id() == runtime::kFunctionSetLed) {
        (*out->mutable_results())["enabled"] = make_bool_value(set_led_enabled);
    }

    runtime::record_call_result(request.device_id(), function_spec->name(), true, "");
    set_status_ok(response);
}

void handle_get_health(const GetHealthRequest &, Response &response) {
    const runtime::RuntimeState state = runtime::snapshot();
    auto *out = response.mutable_get_health();
    *out->mutable_provider() = health::make_provider_health(state);
    for(const auto &device_health : health::make_device_health(state)) {
        *out->add_devices() = device_health;
    }
    set_status_ok(response);
}

void handle_unimplemented(Response &response, const std::string &message) {
    set_status(response, Status::CODE_UNIMPLEMENTED, message);
}

} // namespace anolis_provider_ezo::handlers
