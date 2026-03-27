#pragma once

#include <cstdint>

#include "ezo_i2c.h"
#include "i2c/session.hpp"

namespace anolis_provider_ezo::i2c {

struct EzoDeviceBinding {
    ezo_i2c_device_t device{};
    bool initialized = false;
};

Status bind_ezo_i2c_device(ISession &session,
                           uint8_t address,
                           EzoDeviceBinding &binding);

} // namespace anolis_provider_ezo::i2c
