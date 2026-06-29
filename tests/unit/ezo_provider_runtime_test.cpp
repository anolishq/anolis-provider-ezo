#include "core/ezo_provider_runtime.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "anolis/provider_sdk/result.hpp"
#include "config/provider_config.hpp"
#include "core/runtime_state.hpp"
#include "protocol.pb.h"

// EzoProviderRuntime tests — preserve the ezo-specific coverage lost when
// core/handlers.cpp moved to the SDK: the D1 read conversion (SignalSample ->
// SignalValue + quality_from + rich metadata) and the call validation/dispatch,
// driven in mock mode (no hardware). The generic ADPP envelope/policy is the
// SDK's and is tested there + by conformance.

namespace {

namespace adpp = anolis::deviceprovider::v1;

anolis_provider_ezo::ProviderConfig make_mock_config() {
    anolis_provider_ezo::ProviderConfig config;
    config.provider_name = "ezo-unit-test";
    config.bus_path = "mock://unit-test-i2c";  // mock mode: no hardware
    config.devices = {
        anolis_provider_ezo::DeviceSpec{"ph0", anolis_provider_ezo::EzoDeviceType::Ph, "Tank pH", 0x63},
        anolis_provider_ezo::DeviceSpec{"do0", anolis_provider_ezo::EzoDeviceType::Do, "Tank DO", 0x61},
    };
    return config;
}

anolis_provider_ezo::EzoProviderRuntime make_ready_runtime() {
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(make_mock_config());
    return {};
}

}  // namespace

TEST(EzoProviderRuntimeTest, InventoryAndMetadata) {
    const auto rt = make_ready_runtime();

    const auto meta = rt.metadata();
    EXPECT_EQ(meta.name, "anolis-provider-ezo");
    EXPECT_EQ(meta.protocol_version, "v1");
    EXPECT_EQ(meta.hello_extra.at("coverage"), "all_families");
    EXPECT_EQ(meta.hello_extra.at("discovery_mode"), "manual");

    const auto ids = rt.list_device_ids();
    EXPECT_NE(std::find(ids.begin(), ids.end(), "ph0"), ids.end());
    EXPECT_TRUE(rt.has_device("ph0"));
    EXPECT_FALSE(rt.has_device("nope"));

    // readiness must carry the executable-profile-required init_time_ms (now a
    // real elapsed value, not a hardcoded "0").
    const auto diag = rt.readiness().extra_diagnostics;
    ASSERT_TRUE(diag.contains("init_time_ms"));
    EXPECT_GE(std::stoll(diag.at("init_time_ms")), 0);
}

TEST(EzoProviderRuntimeTest, ReadinessRestoresWaitReadyDiagnostics) {
    const auto rt = make_ready_runtime();
    const auto diag = rt.readiness().extra_diagnostics;

    // ezo-specific WaitReady diagnostics restored after the SDK migration (#88).
    // The SDK separately emits device_count/startup_*/provider_* structurally.
    EXPECT_EQ(diag.at("ready"), "true");
    EXPECT_EQ(diag.at("configured_device_count"), "2");
    EXPECT_EQ(diag.at("active_device_count"), "2");
    EXPECT_EQ(diag.at("excluded_device_count"), "0");
    EXPECT_EQ(diag.at("bus_path"), "mock://unit-test-i2c");
    EXPECT_EQ(diag.at("i2c_executor_running"), "true");
    EXPECT_FALSE(diag.at("i2c_status").empty());
    EXPECT_TRUE(diag.contains("uptime_ms"));
    EXPECT_TRUE(diag.contains("call_success_total"));
    EXPECT_TRUE(diag.contains("call_failure_total"));
    EXPECT_TRUE(diag.contains("i2c_queue_depth"));
    EXPECT_TRUE(diag.contains("i2c_jobs_submitted"));
    EXPECT_TRUE(diag.contains("i2c_jobs_timed_out"));
    EXPECT_TRUE(diag.contains("startup_message"));
}

TEST(EzoProviderRuntimeTest, DeviceHealthRestoresPerDeviceMetrics) {
    // SDK#9: the device_health hook re-supplies ezo's per-device metrics + last_seen.
    auto rt = make_ready_runtime();

    const auto h = rt.device_health("ph0");
    // Static identity + lifetime counters are always present.
    EXPECT_EQ(h.metrics.at("type"), "ph");
    EXPECT_EQ(h.metrics.at("address"), "0x63");
    EXPECT_TRUE(h.metrics.contains("sample_success_count"));
    EXPECT_TRUE(h.metrics.contains("sample_failure_count"));
    EXPECT_TRUE(h.metrics.contains("call_success_count"));
    EXPECT_TRUE(h.metrics.contains("call_failure_count"));
    // ezo samples devices at startup, so a cached sample is present: last_seen and
    // the age metrics are emitted, and (one snapshot) they are mutually consistent.
    EXPECT_TRUE(h.last_seen.has_value());
    EXPECT_TRUE(h.metrics.contains("sample_age_ms"));
    EXPECT_TRUE(h.metrics.contains("sample_stale_after_ms"));

    // An id the SDK may enumerate but that has no live handle is tolerated.
    const auto ghost = rt.device_health("ghost");
    EXPECT_TRUE(ghost.metrics.empty());
    EXPECT_FALSE(ghost.last_seen.has_value());
}

TEST(EzoProviderRuntimeTest, ReadDefaultSetEmitsQualityAndMetadata) {
    auto rt = make_ready_runtime();

    // empty signal_ids -> the curated default set (§7.2), expanded provider-side.
    const anolis::provider_sdk::AdapterReadResult result = rt.read("ph0", {});
    ASSERT_TRUE(result.ok) << result.error_message;
    ASSERT_FALSE(result.values.empty());

    // The D1 conversion: each value carries a quality + the rich per-signal metadata.
    for (const auto& value : result.values) {
        EXPECT_FALSE(value.signal_id().empty());
        EXPECT_NE(value.quality(), adpp::SignalValue::QUALITY_UNSPECIFIED);
        EXPECT_TRUE(value.has_timestamp());
        EXPECT_TRUE(value.metadata().contains("age_ms"));
        EXPECT_TRUE(value.metadata().contains("stale_after_ms"));
    }
}

TEST(EzoProviderRuntimeTest, ReadUnknownDeviceAndSignal) {
    auto rt = make_ready_runtime();

    const auto unknown_device = rt.read("ghost", {});
    EXPECT_FALSE(unknown_device.ok);
    EXPECT_EQ(unknown_device.error_code, adpp::Status::CODE_NOT_FOUND);

    const auto unknown_signal = rt.read("ph0", {"not_a_signal"});
    EXPECT_FALSE(unknown_signal.ok);
    EXPECT_EQ(unknown_signal.error_code, adpp::Status::CODE_NOT_FOUND);
}

TEST(EzoProviderRuntimeTest, CallValidationAndMockDispatch) {
    auto rt = make_ready_runtime();

    // find (function_id 1) takes no args -> mock success.
    const auto find_ok = rt.call("ph0", anolis_provider_ezo::runtime::kFunctionFind, {});
    EXPECT_TRUE(find_ok.ok) << find_ok.error_message;
    EXPECT_EQ(find_ok.error_code, adpp::Status::CODE_OK);

    // set_led (2) requires a bool 'enabled' arg; missing -> INVALID_ARGUMENT.
    const auto bad_args = rt.call("ph0", anolis_provider_ezo::runtime::kFunctionSetLed, {});
    EXPECT_FALSE(bad_args.ok);
    EXPECT_EQ(bad_args.error_code, adpp::Status::CODE_INVALID_ARGUMENT);

    // unknown function_id -> NOT_FOUND.
    const auto unknown = rt.call("ph0", 9999, {});
    EXPECT_EQ(unknown.error_code, adpp::Status::CODE_NOT_FOUND);
}

TEST(EzoProviderRuntimeTest, ResolveFunctionId) {
    const auto rt = make_ready_runtime();
    const auto resolved = rt.resolve_function_id("ph0", "find");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, anolis_provider_ezo::runtime::kFunctionFind);
    EXPECT_FALSE(rt.resolve_function_id("ph0", "no_such_fn").has_value());
}
