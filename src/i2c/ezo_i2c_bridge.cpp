#include "i2c/ezo_i2c_bridge.hpp"

/**
 * @file ezo_i2c_bridge.cpp
 * @brief Glue that adapts the shared I2C bus transport to the EZO C driver
 * callbacks.
 */

#include <cstddef>
#include <string>

namespace anolis_provider_ezo::i2c {
namespace {

ezo_result_t transport_write_then_read(void *context, uint8_t address, const uint8_t *tx_data, size_t tx_len,
                                       uint8_t *rx_data, size_t rx_len, size_t *rx_received) {
    if (context == nullptr) {
        return EZO_ERR_INVALID_ARGUMENT;
    }

    auto *bus = static_cast<I2cBus *>(context);
    // The EZO C library only understands its own result enum, so the transport
    // status is collapsed to ok/transport-error at the bridge boundary.
    const I2cStatus status = bus->write_then_read(address, tx_data, tx_len, rx_data, rx_len, rx_received);
    return status ? EZO_OK : EZO_ERR_TRANSPORT;
}

const ezo_i2c_transport_t *transport_adapter() {
    static const ezo_i2c_transport_t transport = {
        transport_write_then_read,
    };
    return &transport;
}

Status make_status(StatusCode code, const std::string &message) { return Status{code, message}; }

}  // namespace

Status bind_ezo_i2c_device(I2cBus &bus, uint8_t address, EzoDeviceBinding &binding) {
    // Initialization binds the C driver to a borrowed bus pointer; the provider
    // retains ownership and serialized access through the executor.
    const ezo_result_t init_result = ezo_device_init(&binding.device, address, transport_adapter(), &bus);
    if (init_result != EZO_OK) {
        binding.initialized = false;
        return make_status(StatusCode::Internal,
                           std::string("ezo_device_init failed: ") + ezo_result_name(init_result));
    }

    binding.initialized = true;
    return Status::ok();
}

}  // namespace anolis_provider_ezo::i2c
