#include "devices/ph/ph_adapter.hpp"

#include <cstddef>

#include "devices/common/sample_helpers.hpp"

extern "C" {
#include "ezo_ph.h"
}

namespace anolis_provider_ezo::devices::ph {
namespace {

constexpr SignalDefinition kSignals[] = {
    {"ph_value", "pH", "Latest pH measurement", "pH", true},
};

i2c::Status read_sample(ezo_i2c_device_t *device, std::vector<runtime::SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);

    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_ph_send_read_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send pH read");
    }
    wait_for_timing_hint(hint);
    ezo_ph_reading_t reading{};
    result = ezo_ph_read_response_i2c(device, &reading);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read pH response");
    }
    set_signal_value(out, 0, reading.ph);
    return i2c::Status::ok();
}

void build_mock_sample(int address, uint64_t sequence, std::vector<runtime::SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);
    set_signal_value(out, 0, 6.5 + mock_base(address) + mock_delta(sequence));
}

}  // namespace

const DeviceAdapter kAdapter{EZO_PRODUCT_PH,      "sensor.ezo.ph", kSignals,
                             std::size(kSignals), &read_sample,    &build_mock_sample};

}  // namespace anolis_provider_ezo::devices::ph
