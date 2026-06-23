#pragma once

/**
 * @file ezo_i2c_bridge.hpp
 * @brief Bridge from the provider's `ISession` abstraction into the EZO C
 * transport API.
 */

#include <cstdint>

#include "ezo_i2c.h"
#include "i2c/session.hpp"

namespace anolis_provider_ezo::i2c {

/**
 * @brief Bound EZO device handle backed by a provider-managed session
 * transport.
 */
struct EzoDeviceBinding {
    ezo_i2c_device_t device{};
    bool initialized = false;
};

/**
 * @brief Initialize an EZO C device handle that forwards transport calls
 * through `ISession`.
 *
 * The resulting binding borrows `session`; callers must ensure the session
 * outlives any use of `binding.device`.
 */
Status bind_ezo_i2c_device(ISession &session, uint8_t address, EzoDeviceBinding &binding);

}  // namespace anolis_provider_ezo::i2c
