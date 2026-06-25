#pragma once

/**
 * @file adapter_result.hpp
 * @brief Neutral, ADPP-typed results returned by the device read/call execution.
 *
 * `AdapterReadResult` wraps the read's `SignalValue`s with an explicit status
 * channel; `AdapterCallResult` is the status-only call result. This is the shape
 * shared with the other ADPP providers (bread's `AdapterReadResult` /
 * `AdapterCallResult`, sim's `CallResult`) and the eventual provider SDK's
 * neutral result type. Carrying the proto `Status::Code` here means the request
 * handler never maps the provider-local `i2c::StatusCode` (1-stage error map).
 * Lives in the device layer; depends only on the ADPP contract (proto).
 */

#include <string>
#include <vector>

#include "protocol.pb.h"

namespace anolis_provider_ezo::devices {

struct AdapterReadResult {
    bool ok = false;
    anolis::deviceprovider::v1::Status::Code error_code = anolis::deviceprovider::v1::Status::CODE_UNAVAILABLE;
    std::string error_message;
    std::vector<anolis::deviceprovider::v1::SignalValue> values;
};

struct AdapterCallResult {
    bool ok = false;
    anolis::deviceprovider::v1::Status::Code error_code = anolis::deviceprovider::v1::Status::CODE_UNAVAILABLE;
    std::string error_message;
};

}  // namespace anolis_provider_ezo::devices
