#include "core/handlers.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <google/protobuf/timestamp.pb.h>

#include "core/health.hpp"
#include "core/runtime_state.hpp"
#include "core/transport/framed_stdio.hpp"
#include "logging/logger.hpp"

namespace anolis_provider_ezo::handlers {
namespace {

using SignalValue = anolis::deviceprovider::v1::SignalValue;
using Status = anolis::deviceprovider::v1::Status;

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
    return std::chrono::system_clock::time_point(seconds + nanos);
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
    (*hello->mutable_metadata())["phase"] = "4";
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

void handle_call(const CallRequest &, Response &response) {
    set_status(response, Status::CODE_UNIMPLEMENTED, "call is not implemented in phase 4");
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
