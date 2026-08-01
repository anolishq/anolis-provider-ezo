#include "config/provider_config.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "config/config_schema.hpp"

namespace anolis_provider_ezo {
namespace {

class TempConfigFile {
public:
    explicit TempConfigFile(const std::string &yaml_body) {
        static std::atomic<unsigned long long> counter{0ULL};
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto id = counter.fetch_add(1ULL, std::memory_order_relaxed);

        path_ = std::filesystem::temp_directory_path() /
                ("anolis_provider_ezo_config_test_" + std::to_string(nonce) + "_" + std::to_string(id) + ".yaml");

        std::ofstream out(path_);
        if (!out.is_open()) {
            throw std::runtime_error("failed to create temp config: " + path_.string());
        }
        out << yaml_body;
        out.flush();
        if (!out.good()) {
            throw std::runtime_error("failed to write temp config: " + path_.string());
        }
    }

    ~TempConfigFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    const std::filesystem::path &path() const { return path_; }

private:
    std::filesystem::path path_;
};

void expect_config_error(const std::string &yaml_body, const std::string &expected_token) {
    const TempConfigFile config(yaml_body);
    try {
        (void)load_config(config.path().string());
        FAIL() << "Expected load_config() to fail";
    } catch (const std::runtime_error &e) {
        const std::string message = e.what();
        EXPECT_NE(message.find(expected_token), std::string::npos)
            << "expected token: " << expected_token << "\nactual message: " << message;
    }
}

}  // namespace

TEST(ProviderConfigTest, ParsesManualModeWithAllEzoTypes) {
    const TempConfigFile config(R"(
provider:
  name: ezo-lab
hardware:
  bus_path: /dev/i2c-1
discovery:
  mode: manual
devices:
  - id: ph0
    type: ph
    address: 0x63
  - id: orp0
    type: orp
    address: 0x62
  - id: ec0
    type: ec
    address: 0x64
  - id: do0
    type: do
    address: 0x61
  - id: rtd0
    type: rtd
    address: 0x66
  - id: hum0
    type: hum
    address: 0x6f
)");

    const ProviderConfig parsed = load_config(config.path().string());
    EXPECT_EQ(parsed.provider_name, "ezo-lab");
    EXPECT_EQ(parsed.bus_path, "/dev/i2c-1");
    ASSERT_EQ(parsed.devices.size(), 6U);
    EXPECT_EQ(parsed.devices[0].type, EzoDeviceType::Ph);
    EXPECT_EQ(parsed.devices[5].type, EzoDeviceType::Hum);
}

TEST(ProviderConfigTest, SchemaDefaultsAgreeWithBinaryDefaults) {
    // The schema's advertised defaults must be what the binary actually does
    // when the key is absent — a workbench form is rendered from the schema.
    const TempConfigFile config(R"(
hardware:
  bus_path: /dev/i2c-1
discovery:
  mode: manual
)");
    const ProviderConfig parsed = load_config(config.path().string());
    const ProviderConfig defaults;
    EXPECT_EQ(parsed.provider_name, defaults.provider_name);
    EXPECT_EQ(parsed.query_delay_us, defaults.query_delay_us);
    EXPECT_EQ(parsed.timeout_ms, defaults.timeout_ms);
    EXPECT_EQ(parsed.retry_count, defaults.retry_count);

    // ...and the declared schema carries those same values as its defaults.
    const auto &root_spec = provider_schema().root().spec();
    for (const auto &member : root_spec.members) {
        if (member.key == "provider") {
            for (const auto &field : member.object->spec().members) {
                if (field.key == "name") {
                    ASSERT_TRUE(field.field->spec().default_string.has_value());
                    EXPECT_EQ(*field.field->spec().default_string, defaults.provider_name);
                }
            }
        }
        if (member.key == "hardware") {
            for (const auto &field : member.object->spec().members) {
                if (!field.field.has_value()) {
                    continue;
                }
                const auto &spec = field.field->spec();
                if (field.key == "query_delay_us") {
                    EXPECT_EQ(spec.default_int, defaults.query_delay_us);
                }
                if (field.key == "timeout_ms") {
                    EXPECT_EQ(spec.default_int, defaults.timeout_ms);
                }
                if (field.key == "retry_count") {
                    EXPECT_EQ(spec.default_int, defaults.retry_count);
                }
            }
        }
    }
}

TEST(ProviderConfigTest, RejectsDiscoveryModeOtherThanManual) {
    expect_config_error(R"(
hardware:
  bus_path: /dev/i2c-1
discovery:
  mode: scan
)",
                        "discovery.mode");
}

TEST(ProviderConfigTest, RejectsDuplicateAddresses) {
    expect_config_error(R"(
hardware:
  bus_path: /dev/i2c-1
discovery:
  mode: manual
devices:
  - id: ph0
    type: ph
    address: 0x63
  - id: do0
    type: do
    address: 99
)",
                        "duplicate address");
}

TEST(ProviderConfigTest, RejectsUnknownDeviceType) {
    expect_config_error(R"(
hardware:
  bus_path: /dev/i2c-1
discovery:
  mode: manual
devices:
  - id: x0
    type: xyz
    address: 0x61
)",
                        "invalid value");
}

}  // namespace anolis_provider_ezo
