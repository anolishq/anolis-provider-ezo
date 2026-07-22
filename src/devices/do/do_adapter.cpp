#include "devices/do/do_adapter.hpp"

#include <cstddef>

#include "devices/common/sample_helpers.hpp"

extern "C" {
#include "ezo_do.h"
}

namespace anolis_provider_ezo::devices::do_ {
namespace {

constexpr SignalDefinition kSignals[] = {
    {"do_mg_l", "Dissolved Oxygen (mg/L)", "Dissolved oxygen concentration", "mg/L", true},
    {"do_saturation_pct", "Dissolved Oxygen (%)", "Dissolved oxygen percent saturation", "%", false},
};

i2c::Status read_sample(ezo_i2c_device_t *device, std::vector<SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);

    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_do_send_output_query_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send DO output query");
    }
    wait_for_timing_hint(device, hint);
    ezo_do_output_config_t output_config{};
    result = ezo_do_read_output_config_i2c(device, &output_config);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read DO output query");
    }

    hint = ezo_timing_hint_t{};
    result = ezo_do_send_read_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send DO read");
    }
    wait_for_timing_hint(device, hint);

    ezo_do_reading_t reading{};
    result = ezo_do_read_response_i2c(device, output_config.enabled_mask, &reading);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read DO response");
    }

    if ((reading.present_mask & EZO_DO_OUTPUT_MG_L) != 0) {
        set_signal_value(out, 0, reading.milligrams_per_liter);
    } else {
        set_signal_unavailable(out, 0, "mg/l output disabled on device");
    }
    if ((reading.present_mask & EZO_DO_OUTPUT_PERCENT_SATURATION) != 0) {
        set_signal_value(out, 1, reading.percent_saturation);
    } else {
        set_signal_unavailable(out, 1, "saturation output disabled on device");
    }
    return i2c::Status::ok();
}

}  // namespace

const DeviceAdapter kAdapter{EZO_PRODUCT_DO, "sensor.ezo.do", kSignals, std::size(kSignals), &read_sample};

}  // namespace anolis_provider_ezo::devices::do_
