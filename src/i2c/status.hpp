#pragma once

/**
 * @file status.hpp
 * @brief Provider-local operation status used by the bus executor, the EZO
 * bridge, and every device adapter.
 *
 * This is the provider's *general* op-result vocabulary — broader than raw I2C
 * transport (`NotFound`/`DeadlineExceeded`/`Cancelled` are executor/provider
 * concepts). It is deliberately kept distinct from the shared SDK's transport
 * `I2cStatus` (anolis::provider_sdk::i2c): the bus reports whether bytes moved,
 * this reports whether a provider operation succeeded. The boundary mapping
 * lives in bus.hpp.
 *
 * Extracted from the former `session.hpp` when ezo adopted the shared I2C bus
 * seam (anolishq/anolis-provider-sdk#19) so the op-status survives the deletion
 * of the ezo-local session abstraction.
 */

#include <string>

namespace anolis_provider_ezo::i2c {

/**
 * @brief Provider-local status codes for bus/session operations.
 */
enum class StatusCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    Unavailable,
    DeadlineExceeded,
    Cancelled,
    Internal,
};

/**
 * @brief Lightweight status object returned by executor and provider APIs.
 */
struct Status {
    StatusCode code = StatusCode::Ok;
    std::string message = "ok";

    /** @brief Construct the canonical success status. */
    static Status ok() { return Status{}; }

    /** @brief Report whether the status represents success. */
    bool is_ok() const { return code == StatusCode::Ok; }
};

}  // namespace anolis_provider_ezo::i2c
