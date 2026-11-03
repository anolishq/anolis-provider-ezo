#include "devices/do/do_adapter.hpp"

#include <cstddef>
#include <utility>

#include "devices/common/sample_helpers.hpp"

extern "C" {
#include "ezo_do.h"
}

namespace anolis_provider_ezo::devices::do_ {
namespace {

constexpr SignalDefinition kSignals[] = {
    {"do_mg_l", "Dissolved Oxygen (mg/L)", "Dissolved oxygen concentration", "mg/L", true},
    {"do_saturation_pct", "Dissolved Oxygen (%)", "Dissolved oxygen percent saturation", "%", false},
    {"do_calibration_level", "DO calibration level",
     "Points calibrated on the device, from Cal,?: 0 none, 1 one point, 2 two points (zero and atmospheric)", "",
     false},
    {"do_temperature_compensation_c", "DO temperature compensation",
     "Temperature the probe compensates its readings for, from T,?: 20 C until set", "C", false},
    {"do_salinity_compensation_us_cm", "DO salinity compensation",
     "Salinity the probe compensates its readings for, from S,?: 0 until set", "uS/cm", false},
    {"do_pressure_compensation_kpa", "DO pressure compensation",
     "Atmospheric pressure the probe compensates its readings for, from P,?: 101.3 kPa until set", "kPa", false},
};

constexpr int kCalibrationLevelSignal = 2;
constexpr int kTemperatureCompensationSignal = 3;
constexpr int kSalinityCompensationSignal = 4;
constexpr int kPressureCompensationSignal = 5;
// Decimals sent with each compensation value, as in T,37.00, S,5000.0,uS and
// P,95.25.
constexpr uint8_t kTemperatureDecimals = 2;
constexpr uint8_t kSalinityDecimals = 1;
constexpr uint8_t kPressureDecimals = 2;

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

i2c::Status read_calibration_level(ezo_i2c_device_t *device, double *level_out) {
    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_do_send_calibration_query_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send DO calibration query");
    }
    wait_for_timing_hint(device, hint);
    ezo_do_calibration_status_t status{};
    result = ezo_do_read_calibration_status_i2c(device, &status);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read DO calibration status");
    }
    *level_out = static_cast<double>(status.level);
    return i2c::Status::ok();
}

i2c::Status read_compensation(ezo_i2c_device_t *device, CompensationReading *reading_out) {
    CompensationReading reading;

    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_do_send_temperature_query_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send DO temperature compensation query");
    }
    wait_for_timing_hint(device, hint);
    ezo_do_temperature_compensation_t temperature{};
    result = ezo_do_read_temperature_i2c(device, &temperature);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read DO temperature compensation");
    }
    reading.values.temperature_c = temperature.temperature_c;

    hint = ezo_timing_hint_t{};
    result = ezo_do_send_salinity_query_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send DO salinity compensation query");
    }
    wait_for_timing_hint(device, hint);
    ezo_do_salinity_compensation_t salinity{};
    result = ezo_do_read_salinity_i2c(device, &salinity);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read DO salinity compensation");
    }
    if (salinity.unit == EZO_DO_SALINITY_UNIT_MICROSIEMENS) {
        reading.values.salinity_us_cm = salinity.value;
    } else {
        reading.salinity_unavailable = "probe reports its salinity compensation in ppt, not uS/cm";
    }

    hint = ezo_timing_hint_t{};
    result = ezo_do_send_pressure_query_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send DO pressure compensation query");
    }
    wait_for_timing_hint(device, hint);
    ezo_do_pressure_compensation_t pressure{};
    result = ezo_do_read_pressure_i2c(device, &pressure);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read DO pressure compensation");
    }
    reading.values.pressure_kpa = pressure.pressure_kpa;

    *reading_out = std::move(reading);
    return i2c::Status::ok();
}

i2c::Status write_compensation(ezo_i2c_device_t *device, const Compensation &values) {
    // Each value is its own command and response; the first one refused stops
    // the rest.
    auto finish = [device](ezo_result_t result, const ezo_timing_hint_t &hint, const char *context) {
        if (result != EZO_OK) {
            return status_from_ezo_result(result, context);
        }
        wait_for_timing_hint(device, hint);
        return read_command_response(device);
    };
    if (values.temperature_c) {
        ezo_timing_hint_t hint{};
        const i2c::Status status =
            finish(ezo_do_send_temperature_set_i2c(device, *values.temperature_c, kTemperatureDecimals, &hint), hint,
                   "send DO temperature compensation");
        if (!status.is_ok()) {
            return status;
        }
    }
    if (values.salinity_us_cm) {
        ezo_timing_hint_t hint{};
        const i2c::Status status =
            finish(ezo_do_send_salinity_set_i2c(device, *values.salinity_us_cm, EZO_DO_SALINITY_UNIT_MICROSIEMENS,
                                                kSalinityDecimals, &hint),
                   hint, "send DO salinity compensation");
        if (!status.is_ok()) {
            return status;
        }
    }
    if (values.pressure_kpa) {
        ezo_timing_hint_t hint{};
        const i2c::Status status =
            finish(ezo_do_send_pressure_set_i2c(device, *values.pressure_kpa, kPressureDecimals, &hint), hint,
                   "send DO pressure compensation");
        if (!status.is_ok()) {
            return status;
        }
    }
    return i2c::Status::ok();
}

}  // namespace

const DeviceAdapter kAdapter{EZO_PRODUCT_DO,
                             "sensor.ezo.do",
                             kSignals,
                             std::size(kSignals),
                             &read_sample,
                             kCalibrationLevelSignal,
                             &read_calibration_level,
                             kTemperatureCompensationSignal,
                             kSalinityCompensationSignal,
                             kPressureCompensationSignal,
                             &read_compensation,
                             &write_compensation};

}  // namespace anolis_provider_ezo::devices::do_
