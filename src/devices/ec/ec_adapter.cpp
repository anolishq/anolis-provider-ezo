#include "devices/ec/ec_adapter.hpp"

#include <cstddef>

#include "devices/common/sample_helpers.hpp"

extern "C" {
#include "ezo_ec.h"
}

namespace anolis_provider_ezo::devices::ec {
namespace {

constexpr SignalDefinition kSignals[] = {
    {"ec_conductivity_us_cm", "EC Conductivity", "Electrical conductivity", "uS/cm", true},
    {"ec_tds_ppm", "EC TDS", "Total dissolved solids", "ppm", false},
    {"ec_salinity_psu", "EC Salinity", "Salinity", "psu", false},
    {"ec_specific_gravity", "EC Specific Gravity", "Specific gravity", "sg", false},
};

i2c::Status read_sample(ezo_i2c_device_t *device, std::vector<SignalSample> &out) {
    initialize_signal_samples(kSignals, std::size(kSignals), out);

    ezo_timing_hint_t hint{};
    ezo_result_t result = ezo_ec_send_output_query_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send EC output query");
    }
    wait_for_timing_hint(device, hint);
    ezo_ec_output_config_t output_config{};
    result = ezo_ec_read_output_config_i2c(device, &output_config);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read EC output query");
    }

    hint = ezo_timing_hint_t{};
    result = ezo_ec_send_read_i2c(device, &hint);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "send EC read");
    }
    wait_for_timing_hint(device, hint);

    ezo_ec_reading_t reading{};
    result = ezo_ec_read_response_i2c(device, output_config.enabled_mask, &reading);
    if (result != EZO_OK) {
        return status_from_ezo_result(result, "read EC response");
    }

    if ((reading.present_mask & EZO_EC_OUTPUT_CONDUCTIVITY) != 0) {
        set_signal_value(out, 0, reading.conductivity_us_cm);
    } else {
        set_signal_unavailable(out, 0, "conductivity output disabled on device");
    }
    if ((reading.present_mask & EZO_EC_OUTPUT_TOTAL_DISSOLVED_SOLIDS) != 0) {
        set_signal_value(out, 1, reading.total_dissolved_solids_ppm);
    } else {
        set_signal_unavailable(out, 1, "tds output disabled on device");
    }
    if ((reading.present_mask & EZO_EC_OUTPUT_SALINITY) != 0) {
        set_signal_value(out, 2, reading.salinity_ppt);
    } else {
        set_signal_unavailable(out, 2, "salinity output disabled on device");
    }
    if ((reading.present_mask & EZO_EC_OUTPUT_SPECIFIC_GRAVITY) != 0) {
        set_signal_value(out, 3, reading.specific_gravity);
    } else {
        set_signal_unavailable(out, 3, "specific gravity output disabled on device");
    }
    return i2c::Status::ok();
}

}  // namespace

const DeviceAdapter kAdapter{EZO_PRODUCT_EC, "sensor.ezo.ec", kSignals, std::size(kSignals), &read_sample};

}  // namespace anolis_provider_ezo::devices::ec
