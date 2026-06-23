#include "config/provider_config.hpp"

/**
 * @file provider_config.cpp
 * @brief YAML parsing and semantic validation for anolis-provider-ezo
 * configuration.
 */

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <iomanip>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace anolis_provider_ezo {
namespace {

const std::regex kIdentifierPattern("^[A-Za-z0-9_.-]{1,64}$");

void ensure_map(const YAML::Node &node, const std::string &field_name) {
    if (!node || !node.IsMap()) {
        throw std::runtime_error(field_name + " must be a map");
    }
}

void reject_unknown_keys(const YAML::Node &node, const std::string &field_name,
                         const std::set<std::string> &allowed_keys) {
    for (const auto &entry : node) {
        const std::string key = entry.first.as<std::string>();
        if (allowed_keys.find(key) == allowed_keys.end()) {
            throw std::runtime_error("Unknown " + field_name + " key: '" + key + "'");
        }
    }
}

std::string require_scalar(const YAML::Node &node, const std::string &field_name) {
    if (!node || !node.IsScalar()) {
        throw std::runtime_error(field_name + " must be a scalar");
    }

    const std::string value = node.Scalar();
    if (value.empty()) {
        throw std::runtime_error(field_name + " must not be empty");
    }
    return value;
}

void validate_identifier(const std::string &value, const std::string &field_name) {
    if (!std::regex_match(value, kIdentifierPattern)) {
        throw std::runtime_error(field_name + " must match ^[A-Za-z0-9_.-]{1,64}$");
    }
}

int parse_int_value(const YAML::Node &node, const std::string &field_name, bool allow_zero) {
    const std::string text = require_scalar(node, field_name);

    try {
        const int value = std::stoi(text, nullptr, 10);
        if (value < 0 || (!allow_zero && value == 0)) {
            throw std::runtime_error(field_name + " must be " + std::string(allow_zero ? "non-negative" : "positive"));
        }
        return value;
    } catch (const std::invalid_argument &) {
        throw std::runtime_error(field_name + " must be an integer");
    } catch (const std::out_of_range &) {
        throw std::runtime_error(field_name + " is out of range");
    }
}

int parse_address_text(const std::string &text, const std::string &field_name) {
    const int base = (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 16 : 10;

    try {
        const int value = std::stoi(text, nullptr, base);
        if (value < 0x08 || value > 0x77) {
            throw std::runtime_error(field_name + " must be in the 0x08-0x77 I2C address range");
        }
        return value;
    } catch (const std::invalid_argument &) {
        throw std::runtime_error(field_name + " must be an integer or hex literal");
    } catch (const std::out_of_range &) {
        throw std::runtime_error(field_name + " is out of range");
    }
}

int parse_address_value(const YAML::Node &node, const std::string &field_name) {
    return parse_address_text(require_scalar(node, field_name), field_name);
}

}  // namespace

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

    throw std::runtime_error("Invalid devices[].type: '" + value + "'");
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
        throw std::runtime_error("Failed to parse config '" + path + "': " + e.what());
    }

    ensure_map(root, "root");
    // Keep the accepted schema intentionally narrow so manual-config drift is
    // rejected before startup touches the shared I2C bus.
    reject_unknown_keys(root, "root", {"provider", "hardware", "discovery", "devices"});

    ProviderConfig config;
    config.config_file_path = std::filesystem::absolute(path).string();

    const YAML::Node provider_node = root["provider"];
    if (provider_node) {
        ensure_map(provider_node, "provider");
        reject_unknown_keys(provider_node, "provider", {"name"});
        if (provider_node["name"]) {
            config.provider_name = require_scalar(provider_node["name"], "provider.name");
            validate_identifier(config.provider_name, "provider.name");
        }
    }

    const YAML::Node hardware_node = root["hardware"];
    if (!hardware_node) {
        throw std::runtime_error("Missing required section: hardware");
    }
    ensure_map(hardware_node, "hardware");
    reject_unknown_keys(hardware_node, "hardware", {"bus_path", "query_delay_us", "timeout_ms", "retry_count"});

    config.bus_path = require_scalar(hardware_node["bus_path"], "hardware.bus_path");
    if (hardware_node["query_delay_us"]) {
        config.query_delay_us = parse_int_value(hardware_node["query_delay_us"], "hardware.query_delay_us", false);
    }
    if (hardware_node["timeout_ms"]) {
        config.timeout_ms = parse_int_value(hardware_node["timeout_ms"], "hardware.timeout_ms", false);
    }
    if (hardware_node["retry_count"]) {
        config.retry_count = parse_int_value(hardware_node["retry_count"], "hardware.retry_count", true);
    }

    const YAML::Node discovery_node = root["discovery"];
    if (!discovery_node) {
        throw std::runtime_error("Missing required section: discovery");
    }
    ensure_map(discovery_node, "discovery");
    reject_unknown_keys(discovery_node, "discovery", {"mode"});

    const std::string mode = require_scalar(discovery_node["mode"], "discovery.mode");
    // EZO v1 is deliberately manual-only so every runtime device has a known
    // expected identity before the provider probes the bus.
    if (mode != "manual") {
        throw std::runtime_error("discovery.mode must be 'manual' for anolis-provider-ezo v1");
    }

    const YAML::Node devices_node = root["devices"];
    if (devices_node) {
        if (!devices_node.IsSequence()) {
            throw std::runtime_error("devices must be a sequence");
        }

        std::set<std::string> seen_ids;
        std::set<int> seen_addresses;

        for (std::size_t i = 0; i < devices_node.size(); ++i) {
            const YAML::Node device_node = devices_node[i];
            if (!device_node.IsMap()) {
                throw std::runtime_error("devices[" + std::to_string(i) + "] must be a map");
            }

            reject_unknown_keys(device_node, "devices[" + std::to_string(i) + "]", {"id", "type", "label", "address"});

            if (!device_node["id"]) {
                throw std::runtime_error("devices[" + std::to_string(i) + "].id is required");
            }
            if (!device_node["type"]) {
                throw std::runtime_error("devices[" + std::to_string(i) + "].type is required");
            }
            if (!device_node["address"]) {
                throw std::runtime_error("devices[" + std::to_string(i) + "].address is required");
            }

            DeviceSpec spec;
            spec.id = require_scalar(device_node["id"], "devices[" + std::to_string(i) + "].id");
            validate_identifier(spec.id, "devices[" + std::to_string(i) + "].id");
            spec.type =
                parse_device_type(require_scalar(device_node["type"], "devices[" + std::to_string(i) + "].type"));
            spec.label = device_node["label"]
                             ? require_scalar(device_node["label"], "devices[" + std::to_string(i) + "].label")
                             : spec.id;
            spec.address = parse_address_value(device_node["address"], "devices[" + std::to_string(i) + "].address");

            // IDs and addresses must be unique because health, call routing,
            // and startup exclusion diagnostics all key off these identities.
            if (!seen_ids.insert(spec.id).second) {
                throw std::runtime_error("Duplicate devices[].id: '" + spec.id + "'");
            }
            if (!seen_addresses.insert(spec.address).second) {
                throw std::runtime_error("Duplicate devices[].address: '" + format_i2c_address(spec.address) + "'");
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
        << ", hardware.query_delay_us=" << config.query_delay_us << ", hardware.timeout_ms=" << config.timeout_ms
        << ", hardware.retry_count=" << config.retry_count << ", discovery.mode=manual"
        << ", devices=" << config.devices.size();

    return out.str();
}

}  // namespace anolis_provider_ezo
