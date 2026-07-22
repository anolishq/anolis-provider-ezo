#pragma once

/**
 * @file ezo_i2c_bridge.hpp
 * @brief Bridge from the shared `I2cBus` transport into the EZO C transport API.
 */

#include <cstdint>

#include "ezo_i2c.h"
#include "i2c/bus.hpp"

namespace anolis_provider_ezo::i2c {

/**
 * @brief Bound EZO device handle backed by a provider-managed bus transport.
 */
struct EzoDeviceBinding {
    ezo_i2c_device_t device{};
    bool initialized = false;
};

/**
 * @brief Initialize an EZO C device handle that forwards transport calls
 * through `I2cBus`.
 *
 * The resulting binding borrows `bus`; callers must ensure the bus outlives any
 * use of `binding.device`.
 */
Status bind_ezo_i2c_device(I2cBus &bus, uint8_t address, EzoDeviceBinding &binding);

}  // namespace anolis_provider_ezo::i2c
