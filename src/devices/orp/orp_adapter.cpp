#include "devices/orp/orp_adapter.hpp"

#include <cstddef>

#include "devices/common/sample_helpers.hpp"

extern "C" {
#include "ezo_orp.h"
}

namespace anolis_provider_ezo::devices::orp {
namespace {

constexpr SignalDefinition kSignals[] = {
    {"orp_millivolts", "ORP", "Latest ORP measurement", "mV", true},
};

i2c::Status read_sample(ezo_i2c_device_t *device, std::vector<SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);

    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_orp_send_read_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send ORP read");
    }
    wait_for_timing_hint(hint);
    ezo_orp_reading_t reading{};
    result = ezo_orp_read_response_i2c(device, &reading);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read ORP response");
    }
    set_signal_value(out, 0, reading.millivolts);
    return i2c::Status::ok();
}

void build_mock_sample(int address, uint64_t sequence, std::vector<SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);
    set_signal_value(out, 0, 250.0 + (mock_base(address) * 10.0) + (mock_delta(sequence) * 100.0));
}

}  // namespace

const DeviceAdapter kAdapter{EZO_PRODUCT_ORP,     "sensor.ezo.orp", kSignals,
                             std::size(kSignals), &read_sample,     &build_mock_sample};

}  // namespace anolis_provider_ezo::devices::orp
