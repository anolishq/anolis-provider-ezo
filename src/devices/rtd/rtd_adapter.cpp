#include "devices/rtd/rtd_adapter.hpp"

#include <cstddef>

#include "devices/common/sample_helpers.hpp"

extern "C" {
#include "ezo_rtd.h"
}

namespace anolis_provider_ezo::devices::rtd {
namespace {

constexpr SignalDefinition kSignals[] = {
    {"rtd_temperature_c", "RTD Temperature", "Temperature reading", "C", true},
};

i2c::Status read_sample(ezo_i2c_device_t *device, std::vector<SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);

    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_rtd_send_read_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send RTD read");
    }
    wait_for_timing_hint(hint);
    ezo_rtd_reading_t reading{};
    result = ezo_rtd_read_response_i2c(device, EZO_RTD_SCALE_CELSIUS, &reading);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read RTD response");
    }
    set_signal_value(out, 0, reading.temperature);
    return i2c::Status::ok();
}

void build_mock_sample(int address, uint64_t sequence, std::vector<SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);
    set_signal_value(out, 0, 20.0 + mock_base(address) + mock_delta(sequence));
}

}  // namespace

const DeviceAdapter kAdapter{EZO_PRODUCT_RTD,     "sensor.ezo.rtd", kSignals,
                             std::size(kSignals), &read_sample,     &build_mock_sample};

}  // namespace anolis_provider_ezo::devices::rtd
