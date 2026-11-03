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
    {"ph_calibration_level", "pH calibration level",
     "Points calibrated on the device, from Cal,?: 0 none, 1 mid, 2 mid and low, 3 mid, low and high", "", false},
    {"ph_temperature_compensation_c", "pH temperature compensation",
     "Temperature the probe compensates its readings for, from T,?: 25 C until set", "C", false},
};

constexpr int kCalibrationLevelSignal = 1;
constexpr int kTemperatureCompensationSignal = 2;
// Decimals sent with a compensation temperature, as in T,37.00.
constexpr uint8_t kTemperatureDecimals = 2;

i2c::Status read_sample(ezo_i2c_device_t *device, std::vector<SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);

    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_ph_send_read_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send pH read");
    }
    wait_for_timing_hint(device, hint);
    ezo_ph_reading_t reading{};
    result = ezo_ph_read_response_i2c(device, &reading);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read pH response");
    }
    set_signal_value(out, 0, reading.ph);
    return i2c::Status::ok();
}

i2c::Status read_calibration_level(ezo_i2c_device_t *device, double *level_out) {
    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_ph_send_calibration_query_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send pH calibration query");
    }
    wait_for_timing_hint(device, hint);
    ezo_ph_calibration_status_t status{};
    result = ezo_ph_read_calibration_status_i2c(device, &status);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read pH calibration status");
    }
    *level_out = static_cast<double>(status.level);
    return i2c::Status::ok();
}

i2c::Status read_compensation(ezo_i2c_device_t *device, CompensationReading *reading_out) {
    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_ph_send_temperature_query_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send pH temperature compensation query");
    }
    wait_for_timing_hint(device, hint);
    ezo_ph_temperature_compensation_t temperature{};
    result = ezo_ph_read_temperature_i2c(device, &temperature);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read pH temperature compensation");
    }
    *reading_out = CompensationReading{};
    reading_out->values.temperature_c = temperature.temperature_c;
    return i2c::Status::ok();
}

i2c::Status write_compensation(ezo_i2c_device_t *device, const Compensation &values) {
    if (!values.temperature_c) {
        return i2c::Status::ok();
    }
    ezo_timing_hint_t hint{};
    const ezo_result_t result =
        ezo_ph_send_temperature_set_i2c(device, *values.temperature_c, kTemperatureDecimals, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send pH temperature compensation");
    }
    wait_for_timing_hint(device, hint);
    return read_command_response(device);
}

}  // namespace

const DeviceAdapter kAdapter{EZO_PRODUCT_PH,
                             "sensor.ezo.ph",
                             kSignals,
                             std::size(kSignals),
                             &read_sample,
                             kCalibrationLevelSignal,
                             &read_calibration_level,
                             kTemperatureCompensationSignal,
                             -1,
                             -1,
                             &read_compensation,
                             &write_compensation};

}  // namespace anolis_provider_ezo::devices::ph
