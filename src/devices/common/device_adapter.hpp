#pragma once

/**
 * @file device_adapter.hpp
 * @brief The uniform per-family device-adapter descriptor and the single
 * dispatch point that maps an EzoDeviceType to its adapter.
 *
 * Each EZO sensor family is a small adapter module (`devices/<family>/`)
 * exposing its metadata plus stateless read/mock functions through this
 * descriptor. `adapter_for()` is the one place device-type dispatch happens —
 * a compile-time-exhaustive switch, not a registry (RuntimeState is value
 * copied, so adapters are static, stateless, and never per-device owned).
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include "devices/common/device_type.hpp"
#include "devices/common/signal_definition.hpp"
#include "devices/common/signal_sample.hpp"
#include "i2c/status.hpp"

extern "C" {
#include "ezo_i2c.h"
#include "ezo_product.h"
}

namespace anolis_provider_ezo::devices {

struct DeviceAdapter {
    ezo_product_id_t expected_product;
    const char *type_id;
    const SignalDefinition *signals;
    std::size_t signal_count;
    i2c::Status (*read_sample)(ezo_i2c_device_t *device, std::vector<SignalSample> &out);
    void (*build_mock_sample)(int address, uint64_t sequence, std::vector<SignalSample> &out);
};

/** @brief The adapter for a device family. Exhaustive over EzoDeviceType. */
const DeviceAdapter &adapter_for(EzoDeviceType type);

}  // namespace anolis_provider_ezo::devices
