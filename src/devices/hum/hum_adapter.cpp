#include "devices/hum/hum_adapter.hpp"

#include <cstddef>

#include "devices/common/sample_helpers.hpp"

extern "C" {
#include "ezo_hum.h"
}

namespace anolis_provider_ezo::devices::hum {
namespace {

constexpr SignalDefinition kSignals[] = {
    {"hum_relative_humidity_pct", "Humidity", "Relative humidity", "%", true},
    {"hum_temperature_c", "Air Temperature", "Ambient air temperature", "C", true},
    {"hum_dew_point_c", "Dew Point", "Dew point temperature", "C", false},
};

i2c::Status read_sample(ezo_i2c_device_t *device, std::vector<SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);

    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_hum_send_output_query_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send HUM output query");
    }
    wait_for_timing_hint(device, hint);
    ezo_hum_output_config_t output_config{};
    result = ezo_hum_read_output_config_i2c(device, &output_config);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read HUM output query");
    }

    hint = ezo_timing_hint_t{};
    result = ezo_hum_send_read_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send HUM read");
    }
    wait_for_timing_hint(device, hint);

    ezo_hum_reading_t reading{};
    result = ezo_hum_read_response_i2c(device, output_config.enabled_mask, &reading);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read HUM response");
    }

    if ((reading.present_mask & EZO_HUM_OUTPUT_HUMIDITY) != 0) {
        set_signal_value(out, 0, reading.relative_humidity_percent);
    } else {
        set_signal_unavailable(out, 0, "humidity output disabled on device");
    }
    if ((reading.present_mask & EZO_HUM_OUTPUT_AIR_TEMPERATURE) != 0) {
        set_signal_value(out, 1, reading.air_temperature_c);
    } else {
        set_signal_unavailable(out, 1, "air temperature output disabled on device");
    }
    if ((reading.present_mask & EZO_HUM_OUTPUT_DEW_POINT) != 0) {
        set_signal_value(out, 2, reading.dew_point_c);
    } else {
        set_signal_unavailable(out, 2, "dew point output disabled on device");
    }
    return i2c::Status::ok();
}

}  // namespace

const DeviceAdapter kAdapter{EZO_PRODUCT_HUM, "sensor.ezo.hum", kSignals, std::size(kSignals), &read_sample};

}  // namespace anolis_provider_ezo::devices::hum
