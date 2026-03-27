#include "i2c/ezo_i2c_bridge.hpp"

#include <cstddef>
#include <string>

namespace anolis_provider_ezo::i2c {
namespace {

ezo_result_t transport_write_then_read(void *context,
                                       uint8_t address,
                                       const uint8_t *tx_data,
                                       size_t tx_len,
                                       uint8_t *rx_data,
                                       size_t rx_len,
                                       size_t *rx_received) {
    if(context == nullptr) {
        return EZO_ERR_INVALID_ARGUMENT;
    }

    auto *session = static_cast<ISession *>(context);
    const Status status = session->write_then_read(address,
                                                   tx_data,
                                                   tx_len,
                                                   rx_data,
                                                   rx_len,
                                                   rx_received);
    return status.is_ok() ? EZO_OK : EZO_ERR_TRANSPORT;
}

const ezo_i2c_transport_t *transport_adapter() {
    static const ezo_i2c_transport_t transport = {
        transport_write_then_read,
    };
    return &transport;
}

Status make_status(StatusCode code, const std::string &message) {
    return Status{code, message};
}

} // namespace

Status bind_ezo_i2c_device(ISession &session,
                           uint8_t address,
                           EzoDeviceBinding &binding) {
    const ezo_result_t init_result =
        ezo_device_init(&binding.device, address, transport_adapter(), &session);
    if(init_result != EZO_OK) {
        binding.initialized = false;
        return make_status(StatusCode::Internal,
                           std::string("ezo_device_init failed: ") +
                               ezo_result_name(init_result));
    }

    binding.initialized = true;
    return Status::ok();
}

} // namespace anolis_provider_ezo::i2c
