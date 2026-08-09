#include "config/provider_config.hpp"

/**
 * @file provider_config.cpp
 * @brief Config loading for anolis-provider-ezo, driven by the declare-once
 * schema (config_schema.cpp).
 *
 * Validation runs the SDK validator against the SAME schema `--config-schema`
 * advertises, and value extraction uses the SDK's typed helpers (the same
 * scalar resolver as the validator) — so the advertised contract, the enforced
 * validation, and the parsed values cannot drift apart.
 */

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <format>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include "anolis/provider_sdk/config_validate.hpp"
#include "config/config_schema.hpp"

namespace anolis_provider_ezo {

namespace sdkcfg = anolis::provider_sdk::config;

EzoDeviceType parse_device_type(const std::string &value) {
    if (value == "ph") {
        return EzoDeviceType::Ph;
    }
    if (value == "orp") {
        return EzoDeviceType::Orp;
    }
    if (value == "ec") {
        return EzoDeviceType::Ec;
    }
    if (value == "do") {
        return EzoDeviceType::Do;
    }
    if (value == "rtd") {
        return EzoDeviceType::Rtd;
    }
    if (value == "hum") {
        return EzoDeviceType::Hum;
    }

    throw std::runtime_error(std::format("Invalid devices[].type: '{}'", value));
}

std::string to_string(EzoDeviceType type) {
    switch (type) {
        case EzoDeviceType::Ph:
            return "ph";
        case EzoDeviceType::Orp:
            return "orp";
        case EzoDeviceType::Ec:
            return "ec";
        case EzoDeviceType::Do:
            return "do";
        case EzoDeviceType::Rtd:
            return "rtd";
        case EzoDeviceType::Hum:
            return "hum";
    }

    return "unknown";
}

std::string format_i2c_address(int address) {
    std::ostringstream out;
    out << "0x" << std::nouppercase << std::hex << std::setw(2) << std::setfill('0') << address;
    return out.str();
}

ProviderConfig load_config(const std::string &path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception &e) {
        throw std::runtime_error(std::format("Failed to parse config '{}': {}", path, e.what()));
    }

    // Declare-once validation: every structural/semantic error, collected at
    // once, against the schema `--config-schema` advertises.
    const auto errors = sdkcfg::validate(provider_schema(), root);
    if (!errors.empty()) {
        throw std::runtime_error(std::format("Invalid config '{}':\n{}", path, sdkcfg::format_errors(errors)));
    }

    ProviderConfig config;
    config.config_file_path = std::filesystem::absolute(path).string();

    // Post-validation extraction with the SDK's typed helpers — validation
    // guarantees presence/type for required fields, so absent optionals are
    // the only nullopt cases here.
    if (const auto name = sdkcfg::as_string(root["provider"]["name"])) {
        config.provider_name = *name;
    }

    const YAML::Node hardware = root["hardware"];
    if (const auto bus_path = sdkcfg::as_string(hardware["bus_path"])) {
        config.bus_path = *bus_path;
    }
    if (const auto value = sdkcfg::as_int64(hardware["query_delay_us"])) {
        config.query_delay_us = static_cast<int>(*value);
    }
    if (const auto value = sdkcfg::as_int64(hardware["sample_interval_ms"])) {
        config.sample_interval_ms = static_cast<int>(*value);
    }
    if (const auto value = sdkcfg::as_int64(hardware["timeout_ms"])) {
        config.timeout_ms = static_cast<int>(*value);
    }
    if (const auto value = sdkcfg::as_int64(hardware["retry_count"])) {
        config.retry_count = static_cast<int>(*value);
    }

    const YAML::Node devices = root["devices"];
    if (devices.IsDefined() && devices.IsSequence()) {
        for (const auto &device_node : devices) {
            DeviceSpec spec;
            if (const auto id = sdkcfg::as_string(device_node["id"])) {
                spec.id = *id;
            }
            if (const auto type = sdkcfg::as_string(device_node["type"])) {
                spec.type = parse_device_type(*type);
            }
            // Dynamic default (label = id) is provider-side by design; the
            // schema documents it in the field description.
            spec.label = sdkcfg::as_string(device_node["label"]).value_or(spec.id);
            if (const auto address = sdkcfg::parse_i2c_address(device_node["address"])) {
                spec.address = *address;
            }
            config.devices.push_back(spec);
        }
    }

    return config;
}

std::string summarize_config(const ProviderConfig &config) {
    std::ostringstream out;
    // Startup logs use a compact summary rather than dumping the full device
    // list; the detailed roster already exists in the config file itself.
    out << "provider.name=" << config.provider_name << ", hardware.bus_path=" << config.bus_path
        << ", hardware.query_delay_us=" << config.query_delay_us
        << ", hardware.sample_interval_ms=" << config.sample_interval_ms
        << ", hardware.timeout_ms=" << config.timeout_ms << ", hardware.retry_count=" << config.retry_count
        << ", discovery.mode=manual"
        << ", devices=" << config.devices.size();

    return out.str();
}

}  // namespace anolis_provider_ezo
