#include "config/config_schema.hpp"

/**
 * @file config_schema.cpp
 * @brief The declare-once config schema for anolis-provider-ezo.
 *
 * Titles, defaults, and placeholders carry the form-authoring knowledge the
 * workbench previously hardcoded in its provider catalog — a client renders
 * config forms from this schema alone (anolis-workbench#270).
 */

namespace anolis_provider_ezo {

namespace cfg = anolis::provider_sdk::config;

namespace {

constexpr const char *kIdentifierPattern = "^[A-Za-z0-9_.-]{1,64}$";

cfg::Schema build_schema() {
    cfg::Object provider(cfg::Openness::Closed);
    provider.field(cfg::string_field("name")
                       .title("Provider name")
                       .description("Stable identifier for this provider instance.")
                       .pattern(kIdentifierPattern)
                       .default_string("anolis-provider-ezo"));

    cfg::Object hardware(cfg::Openness::Closed);
    hardware.field(cfg::string_field("bus_path")
                       .required()
                       .non_empty()
                       .title("I2C bus path")
                       .placeholder("/dev/i2c-1 or mock://name"));
    hardware.field(cfg::integer_field("query_delay_us")
                       .title("Query delay (µs)")
                       .description("Delay between an EZO command write and its reply read. A single "
                                    "transaction's latency — not a sampling cadence (see "
                                    "sample_interval_ms).")
                       .min_int(1)
                       .max_int(2147483647)
                       .default_int(300000));
    hardware.field(cfg::integer_field("sample_interval_ms")
                       .title("Expected sample interval (ms)")
                       .description("How often this provider expects its samples to be refreshed. It has "
                                    "no sampling thread of its own — it samples when read — so set this "
                                    "to the runtime's polling.interval_ms. Signal freshness bounds and "
                                    "the advertised poll hint derive from it.")
                       .min_int(1)
                       .max_int(2147483647)
                       .default_int(2500));
    hardware.field(
        cfg::integer_field("timeout_ms").title("I/O timeout (ms)").min_int(1).max_int(2147483647).default_int(300));
    hardware.field(
        cfg::integer_field("retry_count").title("I/O retries").min_int(0).max_int(2147483647).default_int(2));

    cfg::Object discovery(cfg::Openness::Closed);
    discovery.field(cfg::string_field("mode")
                        .required()
                        .const_value("manual")
                        .title("Discovery mode")
                        .description("EZO v1 is manual-only: every runtime device has a known "
                                     "expected identity before the provider probes the bus."));

    cfg::Object device(cfg::Openness::Closed);
    device.field(cfg::string_field("id")
                     .required()
                     .unique()
                     .title("Device id")
                     .description("Stable device identifier; unique across devices.")
                     .pattern(kIdentifierPattern));
    device.field(cfg::string_field("type")
                     .required()
                     .title("Device type")
                     .enum_value("ph", "pH Sensor")
                     .enum_value("do", "Dissolved Oxygen")
                     .enum_value("ec", "Conductivity")
                     .enum_value("orp", "ORP")
                     .enum_value("rtd", "Temperature (RTD)")
                     .enum_value("hum", "Humidity"));
    device.field(cfg::string_field("label").non_empty().title("Label").description(
        "Human-readable label; defaults to the device id."));
    device.field(cfg::i2c_address_field("address")
                     .required()
                     .unique()
                     .title("I2C address")
                     .description("7-bit address in the 0x08-0x77 range; unique across devices."));

    cfg::Object root(cfg::Openness::Closed);
    root.child("provider", std::move(provider));
    root.child("hardware", std::move(hardware), cfg::Presence::Required);
    root.child("discovery", std::move(discovery), cfg::Presence::Required);
    root.array("devices", cfg::Array::of_objects(std::move(device)));

    return cfg::Schema(std::move(root))
        .title("anolis-provider-ezo configuration")
        .description("Atlas Scientific EZO I2C sensors over a shared bus.");
}

}  // namespace

const cfg::Schema &provider_schema() {
    static const cfg::Schema schema = build_schema();
    return schema;
}

}  // namespace anolis_provider_ezo
