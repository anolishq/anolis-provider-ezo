#pragma once

/**
 * @file provider_config.hpp
 * @brief Manual configuration types for the EZO provider.
 */

#include <string>
#include <vector>

namespace anolis_provider_ezo {

/**
 * @brief Supported EZO device families with fixed signal/function surfaces.
 */
enum class EzoDeviceType {
    Ph,
    Orp,
    Ec,
    Do,
    Rtd,
    Hum,
};

/**
 * @brief Manually configured device entry for startup identity verification.
 *
 * `id` is the provider-local device ID surfaced through ADPP. `type` and
 * `address` together express the expected family and 7-bit I2C address that
 * startup identity checks must confirm.
 */
struct DeviceSpec {
    std::string id;
    EzoDeviceType type = EzoDeviceType::Ph;
    std::string label;
    int address = 0;
};

/**
 * @brief Resolved provider configuration after YAML parsing.
 *
 * The provider is intentionally manual-config only in v1, so `devices`
 * represents the full expected topology on one bus path. `query_delay_us`
 * controls nominal per-device sample cadence, while `timeout_ms` and
 * `retry_count` configure the Linux transport defaults used by the session and
 * bus executor.
 */
struct ProviderConfig {
    std::string config_file_path;
    std::string provider_name = "anolis-provider-ezo";
    std::string bus_path;
    int query_delay_us = 300000;
    int timeout_ms = 300;
    int retry_count = 2;
    std::vector<DeviceSpec> devices;
};

/**
 * @brief Load and validate provider config from disk.
 */
ProviderConfig load_config(const std::string &path);

/** @brief Parse a config string into an EZO device family. */
EzoDeviceType parse_device_type(const std::string &value);

/** @brief Convert a device family enum to its config/debug string form. */
std::string to_string(EzoDeviceType type);

/** @brief Format a 7-bit I2C address as canonical `0xNN` text. */
std::string format_i2c_address(int address);

/** @brief Build a short human-readable summary of a resolved config. */
std::string summarize_config(const ProviderConfig &config);

} // namespace anolis_provider_ezo
