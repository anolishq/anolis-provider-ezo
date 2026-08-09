#pragma once

/**
 * @file runtime_state.hpp
 * @brief Process-wide runtime state and helper APIs for the EZO provider.
 */

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "config/provider_config.hpp"
#include "devices/common/signal_sample.hpp"
#include "i2c/bus_executor.hpp"
#include "protocol.pb.h"

namespace anolis_provider_ezo::runtime {

using CapabilitySet = anolis::deviceprovider::v1::CapabilitySet;
using Device = anolis::deviceprovider::v1::Device;

/**
 * @brief One signal sample slot in the cached device sample.
 */
// The per-signal read result is owned by the device layer; the runtime aliases
// it so its sample cache and existing call sites are unchanged.
using SignalSample = devices::SignalSample;

/**
 * @brief Cached sample and read-history state for one active device.
 */
struct DeviceSampleCache {
    bool has_sample = false;
    bool last_read_ok = false;
    std::chrono::system_clock::time_point sampled_at{};
    std::string last_error;
    uint64_t success_count = 0;
    uint64_t failure_count = 0;
    uint64_t sequence = 0;
    // Monotonic twin of `sampled_at`, used only to measure the gap between two
    // successful samples. `sampled_at` is a system_clock stamp because it goes
    // on the wire, and system_clock is not monotonic — a Pi has no RTC, so NTP
    // steps it forward seconds after boot and a wall-clock gap would be
    // fabricated (ezo#114).
    std::chrono::steady_clock::time_point sampled_at_steady{};
    // Consecutive successful samples whose gap exceeded the freshness bound. A
    // single over-bound gap is not evidence of a misdeclared cadence: the
    // runtime polls every provider and device from one serial loop, so an
    // unrelated device timing out stretches this device's gap without any read
    // here failing. Only a sustained run means the declared interval is wrong.
    uint32_t consecutive_lagging_gaps = 0;
    // Latched so the cadence-mismatch warning is emitted once per device, not
    // once per sample (ezo#114).
    bool stale_gap_warned = false;
    std::vector<SignalSample> signals;
};

/**
 * @brief Fully active device entry exposed by the provider.
 */
struct ActiveDevice {
    DeviceSpec spec;
    Device descriptor;
    CapabilitySet capabilities;
    // [§7.2] Curated default signal set, returned for an empty signal_ids read.
    std::vector<std::string> default_signal_ids;
    std::string startup_product_code;
    std::string startup_firmware_version;
    DeviceSampleCache sample;

    bool has_last_call = false;
    bool last_call_ok = false;
    std::string last_call_function;
    std::string last_call_error;
    std::chrono::system_clock::time_point last_call_at{};
    uint64_t call_success_count = 0;
    uint64_t call_failure_count = 0;
};

/**
 * @brief Configured device excluded during startup with a recorded reason.
 */
struct ExcludedDevice {
    DeviceSpec spec;
    std::string reason;
};

/**
 * @brief Process-wide snapshot of provider runtime state.
 *
 * This state is owned internally as a singleton-style runtime store and is
 * exposed to handlers by copy through `snapshot()`.
 */
struct RuntimeState {
    ProviderConfig config;
    std::vector<ActiveDevice> active_devices;
    std::vector<ExcludedDevice> excluded_devices;
    bool ready = false;
    std::string startup_message;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point ready_at;  // set when ready first becomes true
    bool i2c_executor_running = false;
    i2c::BusExecutorMetrics i2c_metrics;
    std::string i2c_status_message = "not initialized";
};

// [executable-profile §4] Per-type function_ids, contiguous {1..N}. These three
// control functions are common to every EZO family, so each device declares the
// same set numbered from 1.
constexpr uint32_t kFunctionFind = 1;
constexpr uint32_t kFunctionSetLed = 2;
constexpr uint32_t kFunctionSleep = 3;

/** @brief Reset global runtime state and stop any running executor. */
void reset();

/** @brief Initialize runtime state, start the executor, and probe configured
 * devices. */
void initialize(const ProviderConfig &config);

/** @brief Stop the executor and mark runtime state inactive. */
void shutdown();

/** @brief Return a snapshot copy of the current runtime state. */
RuntimeState snapshot();

/**
 * @brief Submit serialized I2C work through the shared executor.
 */
i2c::Status submit_i2c_job(const std::string &job_name, std::chrono::milliseconds timeout, i2c::BusExecutor::Job job);

/**
 * @brief Per-address transport I/O statistics from the live bus (ezo#100).
 *
 * Zeros when no executor/bus is running or the bus carries no
 * traffic (mock builds).
 */
i2c::IoStats io_stats_for(uint8_t address);

/**
 * @brief The latency of one EZO command/reply exchange, in milliseconds.
 *
 * Derived from `hardware.query_delay_us`, floored. Bounds a single I2C job and
 * the window in which a cached sample may be reused instead of re-transacting.
 * It is emphatically **not** a sampling cadence — see `sample_interval_ms`.
 */
int query_latency_ms(const ProviderConfig &config);

/**
 * @brief The refresh cadence this provider expects, in milliseconds.
 *
 * Taken from `hardware.sample_interval_ms`, floored. The provider samples on
 * demand rather than on a thread of its own, so this is the operator's
 * statement of the consumer's poll interval — it cannot be observed here.
 */
int sample_interval_ms(const ProviderConfig &config);

/**
 * @brief Freshness bound for a sample, in milliseconds.
 *
 * Three refresh intervals, floored. Single source of truth: it is both declared
 * to the runtime in `SignalSpec.stale_after_ms` and used for this provider's own
 * STALE decision, so the two cannot disagree (ezo#114).
 */
int stale_after_ms(const ProviderConfig &config);

/** @brief Refresh one device's cached sample immediately. */
i2c::Status refresh_device_sample(const std::string &device_id);

/** @brief Record the last control-call result for one active device. */
void record_call_result(const std::string &device_id, const std::string &function_name, bool ok,
                        const std::string &message);

}  // namespace anolis_provider_ezo::runtime
