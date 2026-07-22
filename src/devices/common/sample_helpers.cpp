#include "devices/common/sample_helpers.hpp"

#include "i2c/bus.hpp"

namespace anolis_provider_ezo::devices {

i2c::Status make_status(i2c::StatusCode code, const std::string &message) { return i2c::Status{code, message}; }

void wait_for_timing_hint(ezo_i2c_device_t *device, const ezo_timing_hint_t &hint) {
    if (hint.wait_ms == 0 || device == nullptr) {
        return;
    }
    // transport_context is the bound I2cBus (see bind_ezo_i2c_device).
    auto *bus = static_cast<i2c::I2cBus *>(device->transport_context);
    if (bus != nullptr) {
        bus->delay_us(hint.wait_ms * 1000U);
    }
}

i2c::Status status_from_ezo_result(ezo_result_t result, const std::string &context) {
    if (result == EZO_OK) {
        return i2c::Status::ok();
    }

    switch (result) {
        case EZO_ERR_INVALID_ARGUMENT:
            return make_status(i2c::StatusCode::InvalidArgument, context + ": " + ezo_result_name(result));
        case EZO_ERR_TRANSPORT:
            return make_status(i2c::StatusCode::Unavailable, context + ": " + ezo_result_name(result));
        case EZO_ERR_BUFFER_TOO_SMALL:
        case EZO_ERR_PROTOCOL:
        case EZO_ERR_PARSE:
            return make_status(i2c::StatusCode::Internal, context + ": " + ezo_result_name(result));
        case EZO_OK:
            break;
    }

    return make_status(i2c::StatusCode::Internal, context + ": " + ezo_result_name(result));
}

void set_signal_value(std::vector<SignalSample> &signals, std::size_t index, double value) {
    if (index >= signals.size()) {
        return;
    }
    signals[index].available = true;
    signals[index].has_value = true;
    signals[index].value = value;
    signals[index].unavailable_reason.clear();
}

void set_signal_unavailable(std::vector<SignalSample> &signals, std::size_t index, const std::string &reason) {
    if (index >= signals.size()) {
        return;
    }
    signals[index].available = false;
    signals[index].has_value = false;
    signals[index].value = 0.0;
    signals[index].unavailable_reason = reason;
}

void initialize_signal_samples(const SignalDefinition *defs, std::size_t count,
                               std::vector<SignalSample> &signals_out) {
    (void)defs;
    signals_out.assign(count, SignalSample{});
}

}  // namespace anolis_provider_ezo::devices
