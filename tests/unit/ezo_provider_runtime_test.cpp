#include "core/ezo_provider_runtime.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
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

TEST(EzoProviderRuntimeTest, DeviceHealthReportsStateForLiveDevice) {
    // ezo#87: a live, freshly-sampled device carries an engaged state override.
    // (The FAULT/STALE branches are trivial in device_health() and the SDK tests
    // pin that a provider-supplied state reaches the wire.)
    auto rt = make_ready_runtime();
    const auto h = rt.device_health("ph0");
    ASSERT_TRUE(h.state.has_value());
    EXPECT_EQ(*h.state, adpp::DeviceHealth::STATE_OK);
    EXPECT_EQ(h.message.value_or(""), "ok");

    // An excluded/unknown id leaves state disengaged so the SDK's UNREACHABLE wins.
    const auto ghost = rt.device_health("ghost");
    EXPECT_FALSE(ghost.state.has_value());
}

TEST(EzoProviderRuntimeTest, ProviderHealthEmitsAggregateMetrics) {
    // ezo#88 Part 1: provider-level aggregate metrics from one snapshot.
    auto rt = make_ready_runtime();
    const auto h = rt.provider_health();
    EXPECT_EQ(h.metrics.at("configured_devices"), "2");
    EXPECT_EQ(h.metrics.at("active_devices"), "2");
    EXPECT_TRUE(h.metrics.contains("excluded_devices"));
    EXPECT_TRUE(h.metrics.contains("call_success_total"));
    EXPECT_TRUE(h.metrics.contains("call_failure_total"));
    EXPECT_EQ(h.metrics.at("bus_path"), "mock://unit-test-i2c");
    EXPECT_TRUE(h.metrics.contains("i2c_jobs_submitted"));
    // Executor is running in mock mode -> no escalated DEGRADED state.
    EXPECT_FALSE(h.state.has_value());
}

// --- ezo#114: freshness derives from the refresh cadence, not an I2C latency ---

TEST(EzoStalenessTest, StalenessIsIndependentOfQueryDelay) {
    // The bug: stale_after_ms was max(query_delay_us/1000, 50) * 3. query_delay_us
    // is how long one EZO chip takes to answer a command — a transaction latency.
    // Deriving freshness from it made the bound move when the transport changed
    // and left it unrelated to how often samples are actually renewed.
    anolis_provider_ezo::ProviderConfig fast = make_mock_config();
    anolis_provider_ezo::ProviderConfig slow = make_mock_config();
    fast.query_delay_us = 300000;  // the shipped bioreactor-v1 value
    slow.query_delay_us = 900000;
    ASSERT_EQ(fast.sample_interval_ms, slow.sample_interval_ms);

    EXPECT_EQ(anolis_provider_ezo::runtime::stale_after_ms(fast), anolis_provider_ezo::runtime::stale_after_ms(slow));

    // ...while the latency helper still tracks it, so the split is real and the
    // transaction budget did not silently become cadence-derived.
    EXPECT_LT(anolis_provider_ezo::runtime::query_latency_ms(fast),
              anolis_provider_ezo::runtime::query_latency_ms(slow));
}

TEST(EzoStalenessTest, StalenessExceedsTheDeclaredRefreshInterval) {
    // The invariant the bench violated: a sample must not be declared stale
    // sooner than it can possibly be refreshed. On pi-g1 the bound was 900 ms
    // against a 2500 ms poll, so both probes sat STALE for ~1.6 s of every
    // 2.5 s cycle while every single read succeeded — 886 flaps in 8m38s.
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.sample_interval_ms = 2500;

    EXPECT_GT(anolis_provider_ezo::runtime::stale_after_ms(config), config.sample_interval_ms);
}

TEST(EzoStalenessTest, StalenessScalesWithTheConfiguredInterval) {
    anolis_provider_ezo::ProviderConfig slow_poller = make_mock_config();
    slow_poller.sample_interval_ms = 10000;

    // Still headroom for a missed refresh at any cadence the operator declares.
    EXPECT_GT(anolis_provider_ezo::runtime::stale_after_ms(slow_poller), slow_poller.sample_interval_ms);
}

TEST(EzoStalenessTest, DeclaredBoundAndOwnDecisionAgree) {
    // The derivation used to be duplicated in runtime_state.cpp (what is declared
    // to the runtime in SignalSpec) and ezo_provider_runtime.cpp (this provider's
    // own STALE verdict). Two copies of one rule can drift; pin that they cannot.
    auto rt = make_ready_runtime();
    const auto state = anolis_provider_ezo::runtime::snapshot();
    ASSERT_FALSE(state.active_devices.empty());

    const auto& capabilities = state.active_devices.front().capabilities;
    ASSERT_GT(capabilities.signals_size(), 0);
    const uint32_t declared = capabilities.signals(0).stale_after_ms();

    const auto health = rt.device_health(state.active_devices.front().spec.id);
    ASSERT_TRUE(health.metrics.contains("sample_stale_after_ms"));
    EXPECT_EQ(std::to_string(declared), health.metrics.at("sample_stale_after_ms"));
}

TEST(EzoStalenessTest, PollHintAdvertisesTheRefreshCadence) {
    // anolis#269 territory: poll_hint_hz came from the same wrong constant, so
    // the provider advertised 1000/300 = 3.33 Hz — an I2C latency dressed up as
    // a cadence — while it was really refreshed every 2500 ms.
    auto rt = make_ready_runtime();
    const auto state = anolis_provider_ezo::runtime::snapshot();
    ASSERT_FALSE(state.active_devices.empty());

    const auto& capabilities = state.active_devices.front().capabilities;
    ASSERT_GT(capabilities.signals_size(), 0);
    const double hint_hz = capabilities.signals(0).poll_hint_hz();

    const double expected_hz =
        1000.0 / static_cast<double>(anolis_provider_ezo::runtime::sample_interval_ms(make_mock_config()));
    EXPECT_DOUBLE_EQ(hint_hz, expected_hz);

    // And the hinted period must fit inside the freshness bound it ships with.
    EXPECT_LT(1000.0 / hint_hz, static_cast<double>(capabilities.signals(0).stale_after_ms()));
}

// --- ezo#114: the cadence-mismatch warning ---

namespace {

// Latches once per device when two consecutive successful samples are further
// apart than the derived freshness bound. Observable via the snapshot, which is
// cheaper and less brittle than capturing stderr.
bool cadence_warned(const std::string& device_id) {
    const auto state = anolis_provider_ezo::runtime::snapshot();
    for (const auto& device : state.active_devices) {
        if (device.spec.id == device_id) {
            return device.sample.stale_gap_warned;
        }
    }
    return false;
}

}  // namespace

TEST(EzoCadenceWarningTest, StaysQuietWhenSamplesKeepUp) {
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.sample_interval_ms = 2500;  // bound 7500 ms; the test takes milliseconds
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);

    ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());
    ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());

    EXPECT_FALSE(cadence_warned("ph0"));
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoCadenceWarningTest, FiresWhenSamplesLagTheDeclaredInterval) {
    // Floored bound is 500 ms; refresh either side of a longer sleep.
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.sample_interval_ms = 1;
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);

    ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());
    ASSERT_FALSE(cadence_warned("ph0"));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());

    EXPECT_TRUE(cadence_warned("ph0"));
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoCadenceWarningTest, IgnoresAGapSpannedByFailedReads) {
    // The regression: the failure branch deliberately does not move the sample
    // stamps, so without requiring the PREVIOUS read to have succeeded, the next
    // success measures a gap covering the whole outage and blames the cadence
    // for a transport fault. Following that advice would inflate the bound and
    // blind real staleness detection — the very outcome ezo#114 argues against.
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.sample_interval_ms = 1;  // bound floors to 500 ms
    // Let startup probe/sample succeed, then fail a long run of reads.
    config.bus_path = "mock://cadence-outage?drop_after=12&drop_for=60";
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);

    ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());

    // Burn through the drop window; these fail and must not move the stamps.
    bool saw_failure = false;
    for (int i = 0; i < 40; ++i) {
        if (!anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok()) {
            saw_failure = true;
            break;
        }
    }
    ASSERT_TRUE(saw_failure) << "fault injection did not produce a failed read";

    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Recover: drive until a read succeeds again.
    bool recovered = false;
    for (int i = 0; i < 200; ++i) {
        if (anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok()) {
            recovered = true;
            break;
        }
    }
    ASSERT_TRUE(recovered) << "device never recovered from the injected fault";

    EXPECT_FALSE(cadence_warned("ph0")) << "an outage was misreported as a cadence mismatch";
    anolis_provider_ezo::runtime::reset();
}
