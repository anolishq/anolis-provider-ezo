#include "core/ezo_provider_runtime.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "anolis/provider_sdk/host_check.hpp"
#include "anolis/provider_sdk/result.hpp"
#include "config/provider_config.hpp"
#include "core/host_check.hpp"
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

TEST(EzoProviderRuntimeTest, SafeFunctionsAreConfigCategory) {
    // ezo#118: find only blinks the LED and has no process effect, so it is
    // CONFIG like set_led and sleep, not ACTUATE.
    auto rt = make_ready_runtime();
    const auto state = anolis_provider_ezo::runtime::snapshot();
    ASSERT_FALSE(state.active_devices.empty());

    bool found_find = false;
    for (const auto& function : state.active_devices.front().capabilities.functions()) {
        EXPECT_EQ(function.policy().category(), adpp::FunctionPolicy_Category_CATEGORY_CONFIG) << function.name();
        if (function.function_id() == anolis_provider_ezo::runtime::kFunctionFind) {
            found_find = true;
        }
    }
    EXPECT_TRUE(found_find);
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

TEST(EzoProviderRuntimeTest, DevicesPublishAnOpaqueClaim) {
    // One claim per device in the SDK's canonical form; the old ownership tags
    // are gone (anolis#318).
    const auto rt = make_ready_runtime();
    const auto tags = rt.device_info("ph0").tags();
    EXPECT_EQ(tags.at("anolis.claim"), "i2c:mock://unit-test-i2c:0x63");
    EXPECT_EQ(tags.count("hw.bus_path"), 0U);
    EXPECT_EQ(tags.count("hw.i2c_address"), 0U);
    EXPECT_EQ(tags.count("bus_path"), 0U);
    EXPECT_EQ(tags.count("i2c_address"), 0U);
}

TEST(EzoProviderRuntimeTest, MockModeHasNoHostRequirements) {
    const auto rt = make_ready_runtime();
    EXPECT_TRUE(anolis_provider_ezo::check_host(make_mock_config()).empty());
    const auto diag = rt.readiness().extra_diagnostics;
    EXPECT_EQ(diag.at("host_check"), "ok");
    EXPECT_FALSE(diag.contains("host_unmet"));
}

#if defined(__linux__)
TEST(EzoProviderRuntimeTest, MissingBusStaysUpNotReady) {
    // A real (non-mock) bus path that does not exist: the executor is never
    // started, every configured device is excluded with the reason, and
    // readiness and provider health say why (executable profile v1 §6).
    auto config = make_mock_config();
    config.bus_path = "/nonexistent-anolis-test/i2c-9";
    ASSERT_EQ(anolis::provider_sdk::host_check::exit_code(anolis_provider_ezo::check_host(config)), 1);

    anolis_provider_ezo::runtime::reset();
    ASSERT_NO_THROW(anolis_provider_ezo::runtime::initialize(config));
    const anolis_provider_ezo::EzoProviderRuntime rt;

    EXPECT_TRUE(rt.list_device_ids().empty());
    const auto r = rt.readiness();
    EXPECT_FALSE(r.ready);
    EXPECT_EQ(r.extra_diagnostics.at("host_check"), "unmet");
    EXPECT_NE(r.extra_diagnostics.at("host_unmet").find("i2c.bus_present"), std::string::npos);
    EXPECT_EQ(r.extra_diagnostics.at("i2c_executor_running"), "false");
    ASSERT_EQ(r.failed_devices.size(), 2U);
    for (const auto& failed : r.failed_devices) {
        EXPECT_NE(failed.reason.find("host requirements unmet"), std::string::npos) << failed.reason;
    }

    const auto h = rt.provider_health();
    ASSERT_TRUE(h.state.has_value());
    EXPECT_EQ(*h.state, adpp::ProviderHealth::STATE_DEGRADED);
    ASSERT_TRUE(h.message.has_value());
    EXPECT_NE(h.message->find("i2c.bus_present"), std::string::npos) << *h.message;

    anolis_provider_ezo::runtime::reset();
}
#endif

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
uint32_t cadence_streak(const std::string& device_id) {
    const auto state = anolis_provider_ezo::runtime::snapshot();
    for (const auto& device : state.active_devices) {
        if (device.spec.id == device_id) {
            return device.sample.consecutive_lagging_gaps;
        }
    }
    return 0;
}

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

TEST(EzoCadenceWarningTest, FiresOnlyAfterASustainedRun) {
    // Floored bound is 500 ms. One over-bound gap is not evidence of a wrong
    // cadence — see IgnoresAnIsolatedLagSpike — so the warning waits for a run.
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.sample_interval_ms = 1;
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);

    ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(cadence_warned("ph0")) << "warned after " << i << " lagging gaps";
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());
    }

    EXPECT_TRUE(cadence_warned("ph0"));
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoCadenceWarningTest, AFailedReadBreaksTheStreak) {
    // The lag counter is only touched in the success branch, whose last_read_ok
    // guard skips the whole block — reset included — on the first success after
    // a failure. So the failure branch must clear the streak itself, or
    // over-bound gaps separated by outages accumulate and eventually latch the
    // warning on three unrelated transients: the failure the streak exists to
    // prevent, reached through the one path it did not cover.
    //
    // drop_after=14&drop_for=1 fails exactly the third refresh below (op counts
    // are global across both devices and the startup probes; verified by
    // sweeping the window). Asserting the counter directly rather than waiting
    // for the latch keeps this from depending on the streak length.
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.sample_interval_ms = 1;  // bound floors to 500 ms
    config.bus_path = "mock://streak-outage?drop_after=14&drop_for=1";
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());
    ASSERT_EQ(cadence_streak("ph0"), 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());
    ASSERT_EQ(cadence_streak("ph0"), 2u);

    ASSERT_FALSE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok())
        << "fault injection did not fail the third refresh";
    EXPECT_EQ(cadence_streak("ph0"), 0u) << "an outage left the lag streak standing";

    EXPECT_FALSE(cadence_warned("ph0"));
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoCadenceWarningTest, IgnoresAnIsolatedLagSpike) {
    // The runtime polls every provider and device from one serial loop, so an
    // unrelated device hitting its RPC timeout stretches this device's gap with
    // no read here failing and last_read_ok still true — neither of the other
    // two guards applies. The bench has already recorded 4.5 s cycles against a
    // 2.5 s nominal. A latch burned by that would suppress the real warning for
    // the life of the process, which is the failure this warning exists to end.
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.sample_interval_ms = 1;
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);

    ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());
    // Two spikes, each followed by a compliant gap that must re-arm the counter.
    for (int i = 0; i < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());
        ASSERT_TRUE(anolis_provider_ezo::runtime::refresh_device_sample("ph0").is_ok());
    }

    EXPECT_FALSE(cadence_warned("ph0")) << "a transient stall was blamed on the cadence";
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

// A job that outlasts its deadline keeps running on the bus worker after the
// caller has returned (#138). Each test drives one kind of job past its
// deadline with injected bus latency, then waits for the job to finish. A job
// that wrote through a reference into the caller's frame would corrupt the
// stack here. ASan reports it as stack-use-after-return and ThreadSanitizer as
// a data race; an unsanitized build may pass.
TEST(EzoJobLifetimeTest, AProbePastItsDeadlineExcludesTheDevice) {
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.devices.resize(1);  // ph0
    // The probe makes 2 bus ops; 2 x 1.1 s overruns its 2 s floor.
    config.bus_path = "mock://late-probe?latency_us=1100000";
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));  // let the probe job finish

    const auto state = anolis_provider_ezo::runtime::snapshot();
    EXPECT_TRUE(state.active_devices.empty());
    ASSERT_EQ(state.excluded_devices.size(), 1u);
    EXPECT_NE(state.excluded_devices[0].reason.find("timed out"), std::string::npos)
        << state.excluded_devices[0].reason;
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoJobLifetimeTest, ASamplePastItsDeadlineLeavesTheCacheEmpty) {
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.devices.erase(config.devices.begin());  // do0
    config.query_delay_us = 50000;                 // sample deadline floors to 1550 ms
    // The probe makes 2 bus ops (0.9 s, inside its 2 s floor); the startup
    // sample initialize() takes makes 4 (1.8 s, past its deadline).
    config.bus_path = "mock://late-sample?latency_us=450000";
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));  // let the sample job finish

    const auto state = anolis_provider_ezo::runtime::snapshot();
    ASSERT_EQ(state.active_devices.size(), 1u);
    const auto& sample = state.active_devices[0].sample;
    EXPECT_FALSE(sample.has_sample);
    EXPECT_NE(sample.last_error.find("timed out"), std::string::npos) << sample.last_error;
    anolis_provider_ezo::runtime::reset();
}

// A control call reads the device's response code after the command's wait
// (#144). The canned bus options in each mock:// query make the device answer
// the way the test needs.
namespace {

anolis_provider_ezo::EzoProviderRuntime make_runtime_on(const std::string& bus_path) {
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.bus_path = bus_path;
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);
    return {};
}

}  // namespace

TEST(EzoControlResponseTest, ACommandLongerThanTheTimeoutReportsItsResult) {
    // Find waits 300 ms on the device, as long as the default timeout_ms.
    auto rt = make_runtime_on("mock://control-wait?sleep_delays=1");
    const auto result = rt.call("ph0", anolis_provider_ezo::runtime::kFunctionFind, {});
    EXPECT_TRUE(result.ok) << result.error_message;
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoControlResponseTest, ARejectedCommandFails) {
    auto rt = make_runtime_on("mock://control-reject?status=Find:2");
    const auto result = rt.call("ph0", anolis_provider_ezo::runtime::kFunctionFind, {});
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, adpp::Status::CODE_INTERNAL);
    EXPECT_NE(result.error_message.find("rejected"), std::string::npos) << result.error_message;
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoControlResponseTest, ARejectedLedCommandFails) {
    auto rt = make_runtime_on("mock://control-reject-led?status=L,1:2");
    google::protobuf::Map<std::string, adpp::Value> args;
    args["enabled"].set_type(adpp::VALUE_TYPE_BOOL);
    args["enabled"].set_bool_value(true);
    const auto result = rt.call("ph0", anolis_provider_ezo::runtime::kFunctionSetLed, args);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error_message.find("rejected"), std::string::npos) << result.error_message;
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoControlResponseTest, AStillProcessingResponseIsReadAgain) {
    // Four 254 answers use every re-read: 300 ms for Find plus 4 x 250 ms
    // apart, which must fit inside the call's deadline.
    auto rt = make_runtime_on("mock://control-pending?sleep_delays=1&pending=Find:4");
    const auto started = std::chrono::steady_clock::now();
    const auto result = rt.call("ph0", anolis_provider_ezo::runtime::kFunctionFind, {});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_TRUE(result.ok) << result.error_message;
    EXPECT_GE(elapsed, std::chrono::milliseconds(1300));
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoControlResponseTest, AResponseThatStaysPendingFails) {
    // One 254 answer more than the re-reads allow.
    auto rt = make_runtime_on("mock://control-stuck?pending=Find:5");
    const auto result = rt.call("ph0", anolis_provider_ezo::runtime::kFunctionFind, {});
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, adpp::Status::CODE_UNAVAILABLE);
    EXPECT_NE(result.error_message.find("still processing"), std::string::npos) << result.error_message;
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoControlResponseTest, NoDataFails) {
    auto rt = make_runtime_on("mock://control-no-data?status=Find:255");
    const auto result = rt.call("ph0", anolis_provider_ezo::runtime::kFunctionFind, {});
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error_code, adpp::Status::CODE_UNAVAILABLE);
    EXPECT_NE(result.error_message.find("no data"), std::string::npos) << result.error_message;
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoControlResponseTest, SleepIsNotReadBack) {
    // The datasheets say not to read the status byte after Sleep, so a device
    // that would reject it on a read still reports success.
    auto rt = make_runtime_on("mock://control-sleep?status=Sleep:2");
    const auto result = rt.call("ph0", anolis_provider_ezo::runtime::kFunctionSleep, {});
    EXPECT_TRUE(result.ok) << result.error_message;
    anolis_provider_ezo::runtime::reset();
}

// pH and DO calibration (#90): calibrate and clear_calibration, MANUAL-only,
// with their args checked before any bus work.
namespace {

adpp::Value string_value(const std::string& text) {
    adpp::Value value;
    value.set_type(adpp::VALUE_TYPE_STRING);
    value.set_string_value(text);
    return value;
}

adpp::Value double_value(double number) {
    adpp::Value value;
    value.set_type(adpp::VALUE_TYPE_DOUBLE);
    value.set_double_value(number);
    return value;
}

google::protobuf::Map<std::string, adpp::Value> ph_args(const std::string& point, double reference_ph) {
    google::protobuf::Map<std::string, adpp::Value> args;
    args["point"] = string_value(point);
    args["reference_ph"] = double_value(reference_ph);
    return args;
}

google::protobuf::Map<std::string, adpp::Value> do_args(const std::string& point) {
    google::protobuf::Map<std::string, adpp::Value> args;
    args["point"] = string_value(point);
    return args;
}

const adpp::FunctionSpec* function_named(const anolis_provider_ezo::runtime::ActiveDevice& device,
                                         const std::string& name) {
    for (const auto& function : device.capabilities.functions()) {
        if (function.name() == name) {
            return &function;
        }
    }
    return nullptr;
}

}  // namespace

TEST(EzoCalibrationTest, PhAndDoDeclareManualOnlyCalibration) {
    anolis_provider_ezo::ProviderConfig config = make_mock_config();
    config.devices.push_back(
        anolis_provider_ezo::DeviceSpec{"rtd0", anolis_provider_ezo::EzoDeviceType::Rtd, "Tank temp", 0x66});
    anolis_provider_ezo::runtime::reset();
    anolis_provider_ezo::runtime::initialize(config);
    const auto state = anolis_provider_ezo::runtime::snapshot();
    ASSERT_EQ(state.active_devices.size(), 3u);

    for (const auto& device : state.active_devices) {
        const auto* calibrate = function_named(device, "calibrate");
        const auto* clear = function_named(device, "clear_calibration");
        if (device.spec.id == "rtd0") {
            EXPECT_EQ(calibrate, nullptr);
            EXPECT_EQ(clear, nullptr);
            continue;
        }
        ASSERT_NE(calibrate, nullptr) << device.spec.id;
        ASSERT_NE(clear, nullptr) << device.spec.id;
        EXPECT_EQ(calibrate->function_id(), anolis_provider_ezo::runtime::kFunctionCalibrate);
        EXPECT_EQ(clear->function_id(), anolis_provider_ezo::runtime::kFunctionClearCalibration);
        for (const auto* function : {calibrate, clear}) {
            ASSERT_EQ(function->policy().allowed_modes_size(), 1) << function->name();
            EXPECT_EQ(function->policy().allowed_modes(0), "MANUAL");
            EXPECT_FALSE(function->policy().is_idempotent());
        }
        const auto& point = calibrate->args(0);
        EXPECT_EQ(point.name(), "point");
        if (device.spec.id == "ph0") {
            EXPECT_EQ(std::vector<std::string>(point.allowed_values().begin(), point.allowed_values().end()),
                      (std::vector<std::string>{"mid", "low", "high"}));
            ASSERT_EQ(calibrate->args_size(), 2);
            EXPECT_EQ(calibrate->args(1).min_double(), 0.0);
            EXPECT_EQ(calibrate->args(1).max_double(), 14.0);
        } else {
            EXPECT_EQ(std::vector<std::string>(point.allowed_values().begin(), point.allowed_values().end()),
                      (std::vector<std::string>{"atmospheric", "zero"}));
            EXPECT_EQ(calibrate->args_size(), 1);
        }
    }
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoCalibrationTest, CalibrateSendsTheDatasheetCommand) {
    // Each case's device rejects exactly the command calibrate should send, so
    // the rejection proves the wire string; an unrejected call succeeds.
    struct Case {
        const char* device;
        uint32_t function_id;
        google::protobuf::Map<std::string, adpp::Value> args;
        const char* command;
    };
    const std::vector<Case> cases = {
        {"ph0", anolis_provider_ezo::runtime::kFunctionCalibrate, ph_args("mid", 7.0), "Cal,mid,7.00"},
        {"ph0", anolis_provider_ezo::runtime::kFunctionCalibrate, ph_args("low", 4.0), "Cal,low,4.00"},
        {"ph0", anolis_provider_ezo::runtime::kFunctionCalibrate, ph_args("high", 10.01), "Cal,high,10.01"},
        {"ph0", anolis_provider_ezo::runtime::kFunctionClearCalibration, {}, "Cal,clear"},
        {"do0", anolis_provider_ezo::runtime::kFunctionCalibrate, do_args("atmospheric"), "Cal"},
        {"do0", anolis_provider_ezo::runtime::kFunctionCalibrate, do_args("zero"), "Cal,0"},
        {"do0", anolis_provider_ezo::runtime::kFunctionClearCalibration, {}, "Cal,clear"},
    };
    for (const auto& c : cases) {
        auto accepting = make_runtime_on("mock://calibrate-ok");
        const auto ok = accepting.call(c.device, c.function_id, c.args);
        EXPECT_TRUE(ok.ok) << c.command << ": " << ok.error_message;

        auto rejecting = make_runtime_on(std::string("mock://calibrate-reject?status=") + c.command + ":2");
        const auto rejected = rejecting.call(c.device, c.function_id, c.args);
        EXPECT_FALSE(rejected.ok) << c.command;
        EXPECT_NE(rejected.error_message.find("rejected"), std::string::npos) << c.command << ": "
                                                                              << rejected.error_message;
    }
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoCalibrationTest, CalibrateChecksItsArgs) {
    auto rt = make_runtime_on("mock://calibrate-args");
    const auto calibrate = anolis_provider_ezo::runtime::kFunctionCalibrate;
    auto expect_code = [&](const char* device, google::protobuf::Map<std::string, adpp::Value> args,
                           adpp::Status::Code code, const char* why) {
        const auto result = rt.call(device, calibrate, args);
        EXPECT_FALSE(result.ok) << why;
        EXPECT_EQ(result.error_code, code) << why << ": " << result.error_message;
    };

    expect_code("ph0", ph_args("middle", 7.0), adpp::Status::CODE_INVALID_ARGUMENT, "unknown point");
    expect_code("ph0", ph_args("mid", -0.01), adpp::Status::CODE_OUT_OF_RANGE, "below 0");
    expect_code("ph0", ph_args("mid", 14.01), adpp::Status::CODE_OUT_OF_RANGE, "above 14");
    // The driver would refuse NaN too, but inside the bus job; the provider
    // refuses it first, with its own message.
    const auto nan = rt.call("ph0", calibrate, ph_args("mid", std::nan("")));
    EXPECT_EQ(nan.error_code, adpp::Status::CODE_INVALID_ARGUMENT) << nan.error_message;
    EXPECT_NE(nan.error_message.find("finite"), std::string::npos) << nan.error_message;
    expect_code("ph0", do_args("mid"), adpp::Status::CODE_INVALID_ARGUMENT, "missing reference_ph");
    expect_code("do0", do_args("mid"), adpp::Status::CODE_INVALID_ARGUMENT, "pH point on DO");
    expect_code("do0", ph_args("zero", 7.0), adpp::Status::CODE_INVALID_ARGUMENT, "extra arg on DO");
    auto wrong_type = ph_args("mid", 7.0);
    wrong_type["reference_ph"] = string_value("7");
    expect_code("ph0", wrong_type, adpp::Status::CODE_INVALID_ARGUMENT, "reference_ph as a string");

    EXPECT_TRUE(rt.call("ph0", calibrate, ph_args("mid", 0.0)).ok) << "0 is inside the bounds";
    EXPECT_TRUE(rt.call("ph0", calibrate, ph_args("mid", 14.0)).ok) << "14 is inside the bounds";
    anolis_provider_ezo::runtime::reset();
}

TEST(EzoCalibrationTest, CalibrationGetsTheCalibrationWait) {
    // A DO calibration waits 1300 ms. With every re-read used (4 x 250 ms) it
    // runs about 2.3 s: past a generic command's 1.8 s deadline, inside the
    // 2.8 s a calibration gets.
    auto rt = make_runtime_on("mock://calibrate-wait?sleep_delays=1&pending=Cal:4");
    const auto result = rt.call("do0", anolis_provider_ezo::runtime::kFunctionCalibrate, do_args("atmospheric"));
    EXPECT_TRUE(result.ok) << result.error_message;
    anolis_provider_ezo::runtime::reset();
}

