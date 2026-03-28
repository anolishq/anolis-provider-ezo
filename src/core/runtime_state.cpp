#include "core/runtime_state.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include "i2c/ezo_i2c_bridge.hpp"
#include "logging/logger.hpp"

extern "C" {
#include "ezo_control.h"
#include "ezo_do.h"
#include "ezo_ec.h"
#include "ezo_hum.h"
#include "ezo_orp.h"
#include "ezo_ph.h"
#include "ezo_product.h"
#include "ezo_rtd.h"
}

namespace anolis_provider_ezo::runtime {
namespace {

std::mutex g_mutex;
RuntimeState g_state;
std::shared_ptr<i2c::BusExecutor> g_executor;

constexpr int kMinSamplePeriodMs = 50;
constexpr int kMinStaleAfterMs = 500;

using ArgSpec = anolis::deviceprovider::v1::ArgSpec;
using FunctionSpec = anolis::deviceprovider::v1::FunctionSpec;
constexpr auto kValueTypeBool = anolis::deviceprovider::v1::VALUE_TYPE_BOOL;
constexpr auto kCategoryConfig =
    anolis::deviceprovider::v1::FunctionPolicy_Category_CATEGORY_CONFIG;
constexpr auto kCategoryActuate =
    anolis::deviceprovider::v1::FunctionPolicy_Category_CATEGORY_ACTUATE;

struct SignalDefinition {
    const char *signal_id;
    const char *name;
    const char *description;
    const char *unit;
};

constexpr SignalDefinition kPhSignals[] = {
    {"ph.value", "pH", "Latest pH measurement", "pH"},
};

constexpr SignalDefinition kOrpSignals[] = {
    {"orp.millivolts", "ORP", "Latest ORP measurement", "mV"},
};

constexpr SignalDefinition kEcSignals[] = {
    {"ec.conductivity_us_cm", "EC Conductivity", "Electrical conductivity", "uS/cm"},
    {"ec.tds_ppm", "EC TDS", "Total dissolved solids", "ppm"},
    {"ec.salinity_psu", "EC Salinity", "Salinity", "psu"},
    {"ec.specific_gravity", "EC Specific Gravity", "Specific gravity", "sg"},
};

constexpr SignalDefinition kDoSignals[] = {
    {"do.mg_l", "Dissolved Oxygen (mg/L)", "Dissolved oxygen concentration", "mg/L"},
    {"do.saturation_pct", "Dissolved Oxygen (%)", "Dissolved oxygen percent saturation", "%"},
};

constexpr SignalDefinition kRtdSignals[] = {
    {"rtd.temperature_c", "RTD Temperature", "Temperature reading", "C"},
};

constexpr SignalDefinition kHumSignals[] = {
    {"hum.relative_humidity_pct", "Humidity", "Relative humidity", "%"},
    {"hum.temperature_c", "Air Temperature", "Ambient air temperature", "C"},
    {"hum.dew_point_c", "Dew Point", "Dew point temperature", "C"},
};

bool has_prefix(const std::string &value, const std::string &prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool is_mock_mode(const ProviderConfig &config) {
    return has_prefix(config.bus_path, "mock://");
}

int sample_period_ms(const ProviderConfig &config) {
    return std::max(config.query_delay_us / 1000, kMinSamplePeriodMs);
}

int stale_after_ms(const ProviderConfig &config) {
    return std::max(sample_period_ms(config) * 3, kMinStaleAfterMs);
}

ezo_product_id_t expected_product_for_type(EzoDeviceType type) {
    switch(type) {
    case EzoDeviceType::Ph:
        return EZO_PRODUCT_PH;
    case EzoDeviceType::Orp:
        return EZO_PRODUCT_ORP;
    case EzoDeviceType::Ec:
        return EZO_PRODUCT_EC;
    case EzoDeviceType::Do:
        return EZO_PRODUCT_DO;
    case EzoDeviceType::Rtd:
        return EZO_PRODUCT_RTD;
    case EzoDeviceType::Hum:
        return EZO_PRODUCT_HUM;
    }

    return EZO_PRODUCT_UNKNOWN;
}

const char *type_id_for_device(EzoDeviceType type) {
    switch(type) {
    case EzoDeviceType::Ph:
        return "sensor.ezo.ph";
    case EzoDeviceType::Orp:
        return "sensor.ezo.orp";
    case EzoDeviceType::Ec:
        return "sensor.ezo.ec";
    case EzoDeviceType::Do:
        return "sensor.ezo.do";
    case EzoDeviceType::Rtd:
        return "sensor.ezo.rtd";
    case EzoDeviceType::Hum:
        return "sensor.ezo.hum";
    }
    return "sensor.ezo.unknown";
}

const SignalDefinition *signal_definitions_for_type(EzoDeviceType type,
                                                    size_t *count_out) {
    const SignalDefinition *defs = nullptr;
    size_t count = 0;

    switch(type) {
    case EzoDeviceType::Ph:
        defs = kPhSignals;
        count = sizeof(kPhSignals) / sizeof(kPhSignals[0]);
        break;
    case EzoDeviceType::Orp:
        defs = kOrpSignals;
        count = sizeof(kOrpSignals) / sizeof(kOrpSignals[0]);
        break;
    case EzoDeviceType::Ec:
        defs = kEcSignals;
        count = sizeof(kEcSignals) / sizeof(kEcSignals[0]);
        break;
    case EzoDeviceType::Do:
        defs = kDoSignals;
        count = sizeof(kDoSignals) / sizeof(kDoSignals[0]);
        break;
    case EzoDeviceType::Rtd:
        defs = kRtdSignals;
        count = sizeof(kRtdSignals) / sizeof(kRtdSignals[0]);
        break;
    case EzoDeviceType::Hum:
        defs = kHumSignals;
        count = sizeof(kHumSignals) / sizeof(kHumSignals[0]);
        break;
    }

    if(count_out != nullptr) {
        *count_out = count;
    }
    return defs;
}

FunctionSpec *add_function_spec(anolis::deviceprovider::v1::CapabilitySet &caps,
                               uint32_t function_id,
                               const char *name,
                               const char *description,
                               anolis::deviceprovider::v1::FunctionPolicy_Category category,
                               bool idempotent) {
    FunctionSpec *function = caps.add_functions();
    function->set_function_id(function_id);
    function->set_name(name);
    function->set_description(description);
    function->mutable_policy()->set_category(category);
    function->mutable_policy()->set_is_idempotent(idempotent);
    function->mutable_policy()->set_requires_lease(false);
    function->mutable_policy()->set_safety_profile("safe_v1");
    return function;
}

void add_arg_spec(FunctionSpec *function,
                  const char *name,
                  anolis::deviceprovider::v1::ValueType type,
                  const char *description,
                  bool required) {
    if(function == nullptr) {
        return;
    }

    ArgSpec *arg = function->add_args();
    arg->set_name(name);
    arg->set_type(type);
    arg->set_description(description);
    arg->set_required(required);
}

void add_result_spec(FunctionSpec *function,
                     const char *name,
                     anolis::deviceprovider::v1::ValueType type,
                     const char *description) {
    if(function == nullptr) {
        return;
    }

    ArgSpec *result = function->add_results();
    result->set_name(name);
    result->set_type(type);
    result->set_description(description);
    result->set_required(true);
}

void add_safe_function_specs(anolis::deviceprovider::v1::CapabilitySet &caps) {
    FunctionSpec *find_fn = add_function_spec(
        caps,
        kFunctionFind,
        "find",
        "Blink the device LED to help identify physical hardware on the bus.",
        kCategoryActuate,
        false);
    add_result_spec(find_fn, "accepted", kValueTypeBool, "true when the command is accepted by the provider");

    FunctionSpec *set_led_fn = add_function_spec(
        caps,
        kFunctionSetLed,
        "set_led",
        "Enable or disable the device status LED.",
        kCategoryConfig,
        true);
    add_arg_spec(set_led_fn, "enabled", kValueTypeBool, "LED state (true=on, false=off)", true);
    add_result_spec(set_led_fn, "enabled", kValueTypeBool, "Echoed requested LED state");
    add_result_spec(set_led_fn, "accepted", kValueTypeBool, "true when the command is accepted by the provider");

    FunctionSpec *sleep_fn = add_function_spec(
        caps,
        kFunctionSleep,
        "sleep",
        "Put the device into low-power sleep mode.",
        kCategoryConfig,
        true);
    add_result_spec(sleep_fn, "accepted", kValueTypeBool, "true when the command is accepted by the provider");
}

i2c::Status make_status(i2c::StatusCode code, const std::string &message) {
    return i2c::Status{code, message};
}

void wait_for_timing_hint(const ezo_timing_hint_t &hint) {
    if(hint.wait_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(hint.wait_ms));
    }
}

i2c::Status status_from_ezo_result(ezo_result_t result, const std::string &context) {
    if(result == EZO_OK) {
        return i2c::Status::ok();
    }

    switch(result) {
    case EZO_ERR_INVALID_ARGUMENT:
        return make_status(i2c::StatusCode::InvalidArgument,
                           context + ": " + ezo_result_name(result));
    case EZO_ERR_TRANSPORT:
        return make_status(i2c::StatusCode::Unavailable,
                           context + ": " + ezo_result_name(result));
    case EZO_ERR_BUFFER_TOO_SMALL:
    case EZO_ERR_PROTOCOL:
    case EZO_ERR_PARSE:
        return make_status(i2c::StatusCode::Internal,
                           context + ": " + ezo_result_name(result));
    case EZO_OK:
        break;
    }

    return make_status(i2c::StatusCode::Internal,
                       context + ": " + ezo_result_name(result));
}

std::unique_ptr<i2c::ISession> make_session(const ProviderConfig &config) {
    if(is_mock_mode(config)) {
        return std::make_unique<i2c::NoopSession>(config.bus_path);
    }

#if defined(__linux__)
    return std::make_unique<i2c::LinuxSession>(
        config.bus_path,
        config.timeout_ms,
        config.retry_count);
#else
    return std::make_unique<i2c::NoopSession>(config.bus_path);
#endif
}

std::vector<ActiveDevice>::iterator find_active_device_unlocked(
    std::vector<ActiveDevice> &devices,
    const std::string &device_id) {
    return std::find_if(
        devices.begin(),
        devices.end(),
        [&device_id](const ActiveDevice &device) { return device.spec.id == device_id; });
}

anolis::deviceprovider::v1::CapabilitySet build_capabilities(const ProviderConfig &config,
                                                             EzoDeviceType type) {
    anolis::deviceprovider::v1::CapabilitySet capabilities;
    size_t def_count = 0;
    const SignalDefinition *defs = signal_definitions_for_type(type, &def_count);
    for(size_t i = 0; i < def_count; ++i) {
        auto *signal = capabilities.add_signals();
        signal->set_signal_id(defs[i].signal_id);
        signal->set_name(defs[i].name);
        signal->set_description(defs[i].description);
        signal->set_value_type(anolis::deviceprovider::v1::VALUE_TYPE_DOUBLE);
        signal->set_unit(defs[i].unit);
        signal->set_poll_hint_hz(1000.0 / static_cast<double>(sample_period_ms(config)));
        signal->set_stale_after_ms(static_cast<uint32_t>(stale_after_ms(config)));
    }
    add_safe_function_specs(capabilities);
    return capabilities;
}

anolis::deviceprovider::v1::Device build_descriptor(const ProviderConfig &config,
                                                    const DeviceSpec &spec) {
    anolis::deviceprovider::v1::Device descriptor;
    const std::string formatted_address = format_i2c_address(spec.address);
    descriptor.set_device_id(spec.id);
    descriptor.set_provider_name("anolis-provider-ezo");
    descriptor.set_type_id(type_id_for_device(spec.type));
    descriptor.set_type_version("1");
    descriptor.set_label(spec.label.empty() ? spec.id : spec.label);
    descriptor.set_address(formatted_address);
    (*descriptor.mutable_tags())["hw.bus_path"] = config.bus_path;
    (*descriptor.mutable_tags())["hw.i2c_address"] = formatted_address;
    (*descriptor.mutable_tags())["bus_path"] = config.bus_path;
    (*descriptor.mutable_tags())["i2c_address"] = formatted_address;
    (*descriptor.mutable_tags())["configured_type"] = to_string(spec.type);
    return descriptor;
}

ezo_product_id_t mock_product_for_address(int address) {
    switch(address) {
    case 0x61:
        return EZO_PRODUCT_DO;
    case 0x62:
        return EZO_PRODUCT_ORP;
    case 0x63:
        return EZO_PRODUCT_PH;
    case 0x64:
        return EZO_PRODUCT_EC;
    case 0x66:
        return EZO_PRODUCT_RTD;
    case 0x6F:
        return EZO_PRODUCT_HUM;
    default:
        return EZO_PRODUCT_UNKNOWN;
    }
}

void fill_mock_identity(int address, ezo_device_info_t *info) {
    if(info == nullptr) {
        return;
    }

    std::memset(info, 0, sizeof(*info));
    info->product_id = mock_product_for_address(address);

    const ezo_product_metadata_t *metadata = ezo_product_get_metadata(info->product_id);
    if(metadata != nullptr && metadata->vendor_short_code != nullptr) {
        std::snprintf(
            info->product_code,
            sizeof(info->product_code),
            "%s",
            metadata->vendor_short_code);
    } else {
        std::snprintf(info->product_code, sizeof(info->product_code), "UNK");
    }
    std::snprintf(info->firmware_version, sizeof(info->firmware_version), "mock-1.0");
}

i2c::Status probe_identity_real(i2c::BusExecutor &executor,
                                const ProviderConfig &config,
                                const DeviceSpec &spec,
                                ezo_device_info_t *info_out) {
    if(info_out == nullptr) {
        return make_status(i2c::StatusCode::InvalidArgument, "probe requires output info");
    }

    ezo_device_info_t info{};
    const ezo_product_id_t expected_product = expected_product_for_type(spec.type);
    const int timeout_ms = std::max(config.timeout_ms, 2000);

    const i2c::Status status = executor.submit(
        "startup_probe:" + spec.id,
        std::chrono::milliseconds(timeout_ms),
        [&](i2c::ISession &session) {
            i2c::EzoDeviceBinding binding;
            i2c::Status bind_status =
                i2c::bind_ezo_i2c_device(session, static_cast<uint8_t>(spec.address), binding);
            if(!bind_status.is_ok()) {
                return bind_status;
            }

            ezo_timing_hint_t hint{};
            const ezo_result_t send_result =
                ezo_control_send_info_query_i2c(&binding.device, expected_product, &hint);
            if(send_result != EZO_OK) {
                return status_from_ezo_result(send_result, "send info query");
            }

            if(hint.wait_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(hint.wait_ms));
            }

            const ezo_result_t read_result = ezo_control_read_info_i2c(&binding.device, &info);
            if(read_result != EZO_OK) {
                return status_from_ezo_result(read_result, "read info response");
            }

            return i2c::Status::ok();
        });

    if(status.is_ok()) {
        *info_out = info;
    }
    return status;
}

void set_signal_value(std::vector<SignalSample> &signals,
                      size_t index,
                      double value) {
    if(index >= signals.size()) {
        return;
    }
    signals[index].available = true;
    signals[index].has_value = true;
    signals[index].value = value;
    signals[index].unavailable_reason.clear();
}

void set_signal_unavailable(std::vector<SignalSample> &signals,
                            size_t index,
                            const std::string &reason) {
    if(index >= signals.size()) {
        return;
    }
    signals[index].available = false;
    signals[index].has_value = false;
    signals[index].value = 0.0;
    signals[index].unavailable_reason = reason;
}

void initialize_signal_samples(EzoDeviceType type,
                               std::vector<SignalSample> *signals_out) {
    if(signals_out == nullptr) {
        return;
    }
    size_t signal_count = 0;
    (void)signal_definitions_for_type(type, &signal_count);
    signals_out->assign(signal_count, SignalSample{});
}

i2c::Status read_sample_from_bound_device(ezo_i2c_device_t *device,
                                          const DeviceSpec &spec,
                                          std::vector<SignalSample> *signals_out) {
    if(device == nullptr || signals_out == nullptr) {
        return make_status(i2c::StatusCode::InvalidArgument,
                           "device sample read requires valid pointers");
    }

    initialize_signal_samples(spec.type, signals_out);

    switch(spec.type) {
    case EzoDeviceType::Ph: {
        ezo_timing_hint_t hint{};
        ezo_result_t result = ezo_ph_send_read_i2c(device, &hint);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "send pH read");
        }
        wait_for_timing_hint(hint);
        ezo_ph_reading_t reading{};
        result = ezo_ph_read_response_i2c(device, &reading);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "read pH response");
        }
        set_signal_value(*signals_out, 0, reading.ph);
        return i2c::Status::ok();
    }
    case EzoDeviceType::Orp: {
        ezo_timing_hint_t hint{};
        ezo_result_t result = ezo_orp_send_read_i2c(device, &hint);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "send ORP read");
        }
        wait_for_timing_hint(hint);
        ezo_orp_reading_t reading{};
        result = ezo_orp_read_response_i2c(device, &reading);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "read ORP response");
        }
        set_signal_value(*signals_out, 0, reading.millivolts);
        return i2c::Status::ok();
    }
    case EzoDeviceType::Rtd: {
        ezo_timing_hint_t hint{};
        ezo_result_t result = ezo_rtd_send_read_i2c(device, &hint);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "send RTD read");
        }
        wait_for_timing_hint(hint);
        ezo_rtd_reading_t reading{};
        result = ezo_rtd_read_response_i2c(device, EZO_RTD_SCALE_CELSIUS, &reading);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "read RTD response");
        }
        set_signal_value(*signals_out, 0, reading.temperature);
        return i2c::Status::ok();
    }
    case EzoDeviceType::Ec: {
        ezo_timing_hint_t hint{};
        ezo_result_t result = ezo_ec_send_output_query_i2c(device, &hint);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "send EC output query");
        }
        wait_for_timing_hint(hint);
        ezo_ec_output_config_t output_config{};
        result = ezo_ec_read_output_config_i2c(device, &output_config);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "read EC output query");
        }

        hint = ezo_timing_hint_t{};
        result = ezo_ec_send_read_i2c(device, &hint);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "send EC read");
        }
        wait_for_timing_hint(hint);

        ezo_ec_reading_t reading{};
        result = ezo_ec_read_response_i2c(device, output_config.enabled_mask, &reading);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "read EC response");
        }

        if((reading.present_mask & EZO_EC_OUTPUT_CONDUCTIVITY) != 0) {
            set_signal_value(*signals_out, 0, reading.conductivity_us_cm);
        } else {
            set_signal_unavailable(*signals_out, 0, "conductivity output disabled on device");
        }
        if((reading.present_mask & EZO_EC_OUTPUT_TOTAL_DISSOLVED_SOLIDS) != 0) {
            set_signal_value(*signals_out, 1, reading.total_dissolved_solids_ppm);
        } else {
            set_signal_unavailable(*signals_out, 1, "tds output disabled on device");
        }
        if((reading.present_mask & EZO_EC_OUTPUT_SALINITY) != 0) {
            set_signal_value(*signals_out, 2, reading.salinity_ppt);
        } else {
            set_signal_unavailable(*signals_out, 2, "salinity output disabled on device");
        }
        if((reading.present_mask & EZO_EC_OUTPUT_SPECIFIC_GRAVITY) != 0) {
            set_signal_value(*signals_out, 3, reading.specific_gravity);
        } else {
            set_signal_unavailable(*signals_out, 3, "specific gravity output disabled on device");
        }
        return i2c::Status::ok();
    }
    case EzoDeviceType::Do: {
        ezo_timing_hint_t hint{};
        ezo_result_t result = ezo_do_send_output_query_i2c(device, &hint);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "send DO output query");
        }
        wait_for_timing_hint(hint);
        ezo_do_output_config_t output_config{};
        result = ezo_do_read_output_config_i2c(device, &output_config);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "read DO output query");
        }

        hint = ezo_timing_hint_t{};
        result = ezo_do_send_read_i2c(device, &hint);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "send DO read");
        }
        wait_for_timing_hint(hint);

        ezo_do_reading_t reading{};
        result = ezo_do_read_response_i2c(device, output_config.enabled_mask, &reading);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "read DO response");
        }

        if((reading.present_mask & EZO_DO_OUTPUT_MG_L) != 0) {
            set_signal_value(*signals_out, 0, reading.milligrams_per_liter);
        } else {
            set_signal_unavailable(*signals_out, 0, "mg/l output disabled on device");
        }
        if((reading.present_mask & EZO_DO_OUTPUT_PERCENT_SATURATION) != 0) {
            set_signal_value(*signals_out, 1, reading.percent_saturation);
        } else {
            set_signal_unavailable(*signals_out, 1, "saturation output disabled on device");
        }
        return i2c::Status::ok();
    }
    case EzoDeviceType::Hum: {
        ezo_timing_hint_t hint{};
        ezo_result_t result = ezo_hum_send_output_query_i2c(device, &hint);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "send HUM output query");
        }
        wait_for_timing_hint(hint);
        ezo_hum_output_config_t output_config{};
        result = ezo_hum_read_output_config_i2c(device, &output_config);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "read HUM output query");
        }

        hint = ezo_timing_hint_t{};
        result = ezo_hum_send_read_i2c(device, &hint);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "send HUM read");
        }
        wait_for_timing_hint(hint);

        ezo_hum_reading_t reading{};
        result = ezo_hum_read_response_i2c(device, output_config.enabled_mask, &reading);
        if(result != EZO_OK) {
            return status_from_ezo_result(result, "read HUM response");
        }

        if((reading.present_mask & EZO_HUM_OUTPUT_HUMIDITY) != 0) {
            set_signal_value(*signals_out, 0, reading.relative_humidity_percent);
        } else {
            set_signal_unavailable(*signals_out, 0, "humidity output disabled on device");
        }
        if((reading.present_mask & EZO_HUM_OUTPUT_AIR_TEMPERATURE) != 0) {
            set_signal_value(*signals_out, 1, reading.air_temperature_c);
        } else {
            set_signal_unavailable(*signals_out, 1, "air temperature output disabled on device");
        }
        if((reading.present_mask & EZO_HUM_OUTPUT_DEW_POINT) != 0) {
            set_signal_value(*signals_out, 2, reading.dew_point_c);
        } else {
            set_signal_unavailable(*signals_out, 2, "dew point output disabled on device");
        }
        return i2c::Status::ok();
    }
    }

    return make_status(i2c::StatusCode::Internal, "unsupported device type in sample read");
}

void build_mock_sample(const DeviceSpec &spec,
                       uint64_t sequence,
                       std::vector<SignalSample> *signals_out) {
    initialize_signal_samples(spec.type, signals_out);
    if(signals_out == nullptr) {
        return;
    }

    const double base = static_cast<double>((spec.address % 17) + 1) * 0.1;
    const double delta = static_cast<double>(sequence % 25) * 0.01;

    switch(spec.type) {
    case EzoDeviceType::Ph:
        set_signal_value(*signals_out, 0, 6.5 + base + delta);
        break;
    case EzoDeviceType::Orp:
        set_signal_value(*signals_out, 0, 250.0 + (base * 10.0) + (delta * 100.0));
        break;
    case EzoDeviceType::Rtd:
        set_signal_value(*signals_out, 0, 20.0 + base + delta);
        break;
    case EzoDeviceType::Ec:
        set_signal_value(*signals_out, 0, 700.0 + (base * 100.0) + (delta * 100.0));
        set_signal_value(*signals_out, 1, 350.0 + (base * 50.0) + (delta * 80.0));
        set_signal_unavailable(*signals_out, 2, "salinity output disabled on device");
        set_signal_unavailable(*signals_out, 3, "specific gravity output disabled on device");
        break;
    case EzoDeviceType::Do:
        set_signal_value(*signals_out, 0, 7.0 + base + delta);
        set_signal_unavailable(*signals_out, 1, "saturation output disabled on device");
        break;
    case EzoDeviceType::Hum:
        set_signal_value(*signals_out, 0, 45.0 + (base * 5.0) + (delta * 10.0));
        set_signal_value(*signals_out, 1, 22.0 + base + delta);
        set_signal_unavailable(*signals_out, 2, "dew point output disabled on device");
        break;
    }
}

std::string build_startup_message(const RuntimeState &state) {
    std::ostringstream out;
    out << "phase5 startup complete: active=" << state.active_devices.size()
        << ", excluded=" << state.excluded_devices.size()
        << ", configured=" << state.config.devices.size();
    if(!state.i2c_status_message.empty()) {
        out << ", i2c=" << state.i2c_status_message;
    }
    return out.str();
}

} // namespace

void shutdown() {
    std::shared_ptr<i2c::BusExecutor> executor;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        executor = std::move(g_executor);
        g_state.i2c_executor_running = false;
    }
    if(executor) {
        executor->stop();
    }
}

void reset() {
    shutdown();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state = RuntimeState{};
}

void initialize(const ProviderConfig &config) {
    reset();

    RuntimeState state;
    state.config = config;
    state.started_at = std::chrono::system_clock::now();
    state.ready = false;

    auto executor = std::make_shared<i2c::BusExecutor>(make_session(config));
    const i2c::Status start_status = executor->start();

    state.i2c_executor_running = start_status.is_ok();
    state.i2c_status_message = start_status.message;
    state.i2c_metrics = executor->snapshot_metrics();
    if(!start_status.is_ok()) {
        state.startup_message = "phase5 startup failed to initialize I2C executor: " +
                                start_status.message;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state = std::move(state);
        return;
    }

    const bool mock_mode = is_mock_mode(config);
    for(const DeviceSpec &spec : config.devices) {
        ezo_device_info_t info{};
        i2c::Status probe_status = i2c::Status::ok();
        if(mock_mode) {
            fill_mock_identity(spec.address, &info);
        } else {
            probe_status = probe_identity_real(*executor, config, spec, &info);
        }

        if(!probe_status.is_ok()) {
            state.excluded_devices.push_back(ExcludedDevice{spec, probe_status.message});
            continue;
        }

        const ezo_product_id_t expected_product = expected_product_for_type(spec.type);
        if(info.product_id != expected_product) {
            std::string actual = "unknown";
            if(const ezo_product_metadata_t *meta = ezo_product_get_metadata(info.product_id)) {
                actual = meta->family_name;
            }

            std::string expected = to_string(spec.type);
            state.excluded_devices.push_back(
                ExcludedDevice{spec, "type mismatch: configured " + expected + ", detected " +
                                         actual});
            continue;
        }

        ActiveDevice device;
        device.spec = spec;
        device.descriptor = build_descriptor(config, spec);
        device.capabilities = build_capabilities(config, spec.type);
        device.startup_product_code = info.product_code;
        device.startup_firmware_version = info.firmware_version;
        device.sample.signals.resize(static_cast<size_t>(device.capabilities.signals_size()));
        (*device.descriptor.mutable_tags())["ezo_product_code"] = device.startup_product_code;
        (*device.descriptor.mutable_tags())["ezo_firmware"] = device.startup_firmware_version;
        state.active_devices.push_back(std::move(device));
    }

    state.ready = !state.active_devices.empty();
    if(state.ready && state.i2c_status_message.empty()) {
        state.i2c_status_message = "ok";
    }
    state.startup_message = build_startup_message(state);

    std::vector<std::string> active_ids;
    active_ids.reserve(state.active_devices.size());
    for(const ActiveDevice &device : state.active_devices) {
        active_ids.push_back(device.spec.id);
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state = std::move(state);
        g_executor = std::move(executor);
    }

    for(const std::string &device_id : active_ids) {
        const i2c::Status refresh_status = refresh_device_sample(device_id);
        if(!refresh_status.is_ok()) {
            logging::warning("startup sample failed for device '" + device_id + "': " +
                             refresh_status.message);
        }
    }
}

RuntimeState snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeState copy = g_state;
    if(g_executor) {
        copy.i2c_executor_running = g_executor->is_running();
        copy.i2c_metrics = g_executor->snapshot_metrics();
        if(copy.i2c_metrics.last_error.empty()) {
            if(copy.i2c_status_message.empty()) {
                copy.i2c_status_message = "ok";
            }
        } else {
            copy.i2c_status_message = copy.i2c_metrics.last_error;
        }
    }
    return copy;
}

i2c::Status submit_i2c_job(const std::string &job_name,
                           std::chrono::milliseconds timeout,
                           i2c::BusExecutor::Job job) {
    std::shared_ptr<i2c::BusExecutor> executor;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        executor = g_executor;
    }

    if(!executor) {
        return i2c::Status{
            i2c::StatusCode::Unavailable,
            "i2c executor is not running",
        };
    }

    return executor->submit(job_name, timeout, std::move(job));
}

i2c::Status refresh_device_sample(const std::string &device_id) {
    if(device_id.empty()) {
        return make_status(i2c::StatusCode::InvalidArgument, "device_id is required");
    }

    ProviderConfig config;
    DeviceSpec spec;
    bool mock_mode = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = find_active_device_unlocked(g_state.active_devices, device_id);
        if(it == g_state.active_devices.end()) {
            return make_status(i2c::StatusCode::NotFound, "unknown device_id");
        }
        config = g_state.config;
        spec = it->spec;
        mock_mode = is_mock_mode(config);
    }

    if(mock_mode) {
        const auto now = std::chrono::system_clock::now();
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = find_active_device_unlocked(g_state.active_devices, device_id);
        if(it == g_state.active_devices.end()) {
            return make_status(i2c::StatusCode::NotFound, "unknown device_id");
        }

        const uint64_t sequence = it->sample.sequence + 1;
        build_mock_sample(spec, sequence, &it->sample.signals);
        it->sample.sampled_at = now;
        it->sample.has_sample = true;
        it->sample.last_read_ok = true;
        it->sample.last_error.clear();
        ++it->sample.success_count;
        it->sample.sequence = sequence;
        return i2c::Status::ok();
    }

    std::vector<SignalSample> new_signals;
    const int timeout_ms = std::max(config.timeout_ms, sample_period_ms(config) + 1500);
    const i2c::Status status = submit_i2c_job(
        "sample:" + device_id,
        std::chrono::milliseconds(timeout_ms),
        [&](i2c::ISession &session) {
            i2c::EzoDeviceBinding binding;
            i2c::Status bind_status =
                i2c::bind_ezo_i2c_device(session, static_cast<uint8_t>(spec.address), binding);
            if(!bind_status.is_ok()) {
                return bind_status;
            }

            return read_sample_from_bound_device(&binding.device, spec, &new_signals);
        });

    const auto now = std::chrono::system_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = find_active_device_unlocked(g_state.active_devices, device_id);
        if(it == g_state.active_devices.end()) {
            return make_status(i2c::StatusCode::NotFound, "unknown device_id");
        }

        if(status.is_ok()) {
            it->sample.signals = std::move(new_signals);
            it->sample.sampled_at = now;
            it->sample.has_sample = true;
            it->sample.last_read_ok = true;
            it->sample.last_error.clear();
            ++it->sample.success_count;
            ++it->sample.sequence;
        } else {
            it->sample.last_read_ok = false;
            it->sample.last_error = status.message;
            ++it->sample.failure_count;
        }
    }
    return status;
}

void record_call_result(const std::string &device_id,
                        const std::string &function_name,
                        bool ok,
                        const std::string &message) {
    if(device_id.empty()) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = find_active_device_unlocked(g_state.active_devices, device_id);
    if(it == g_state.active_devices.end()) {
        return;
    }

    it->has_last_call = true;
    it->last_call_ok = ok;
    it->last_call_function = function_name;
    it->last_call_at = now;

    if(ok) {
        it->last_call_error.clear();
        ++it->call_success_count;
    } else {
        it->last_call_error = message;
        ++it->call_failure_count;
    }
}

} // namespace anolis_provider_ezo::runtime
