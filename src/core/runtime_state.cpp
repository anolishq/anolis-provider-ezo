/**
 * @file runtime_state.cpp
 * @brief Implementation of the provider's process-wide runtime state and
 * startup probes.
 *
 * Startup is intentionally strict but partial-ready: every configured device is
 * identity-checked, mismatches are excluded with diagnostics, and healthy
 * devices still become active behind one serialized I2C executor.
 */

#include "core/runtime_state.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

#include "devices/common/device_adapter.hpp"
#include "devices/common/sample_helpers.hpp"
#include "i2c/ezo_i2c_bridge.hpp"
#include "logging/logger.hpp"

extern "C" {
// Per-family read headers (ezo_ph.h, ezo_ec.h, …) now live in the per-family
// adapter modules; runtime_state only needs control + product identity.
#include "ezo_control.h"
#include "ezo_product.h"
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
constexpr auto kCategoryConfig = anolis::deviceprovider::v1::FunctionPolicy_Category_CATEGORY_CONFIG;
constexpr auto kCategoryActuate = anolis::deviceprovider::v1::FunctionPolicy_Category_CATEGORY_ACTUATE;

// Per-family signal tables, read/mock logic, and EZO error/timing helpers now
// live in the device-adapter modules (src/devices/). Pull the shared helpers
// that retained runtime code still calls into this scope.
using devices::make_status;
using devices::SignalDefinition;
using devices::status_from_ezo_result;

bool has_prefix(const std::string &value, const std::string &prefix) { return value.rfind(prefix, 0) == 0; }

bool is_mock_mode(const ProviderConfig &config) { return has_prefix(config.bus_path, "mock://"); }

int sample_period_ms(const ProviderConfig &config) {
    return std::max(config.query_delay_us / 1000, kMinSamplePeriodMs);
}

int stale_after_ms(const ProviderConfig &config) { return std::max(sample_period_ms(config) * 3, kMinStaleAfterMs); }

ezo_product_id_t expected_product_for_type(EzoDeviceType type) { return devices::adapter_for(type).expected_product; }

const char *type_id_for_device(EzoDeviceType type) { return devices::adapter_for(type).type_id; }

const SignalDefinition *signal_definitions_for_type(EzoDeviceType type, size_t *count_out) {
    const devices::DeviceAdapter &adapter = devices::adapter_for(type);
    if (count_out != nullptr) {
        *count_out = adapter.signal_count;
    }
    return adapter.signals;
}

FunctionSpec *add_function_spec(anolis::deviceprovider::v1::CapabilitySet &caps, uint32_t function_id, const char *name,
                                const char *description, anolis::deviceprovider::v1::FunctionPolicy_Category category,
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

void add_arg_spec(FunctionSpec *function, const char *name, anolis::deviceprovider::v1::ValueType type,
                  const char *description, bool required) {
    if (function == nullptr) {
        return;
    }

    ArgSpec *arg = function->add_args();
    arg->set_name(name);
    arg->set_type(type);
    arg->set_description(description);
    arg->set_required(required);
}

void add_result_spec(FunctionSpec *function, const char *name, anolis::deviceprovider::v1::ValueType type,
                     const char *description) {
    if (function == nullptr) {
        return;
    }

    ArgSpec *result = function->add_results();
    result->set_name(name);
    result->set_type(type);
    result->set_description(description);
    result->set_required(true);
}

void add_safe_function_specs(anolis::deviceprovider::v1::CapabilitySet &caps) {
    FunctionSpec *find_fn = add_function_spec(caps, kFunctionFind, "find",
                                              "Blink the device LED to help identify physical hardware on the bus.",
                                              kCategoryActuate, false);
    add_result_spec(find_fn, "accepted", kValueTypeBool, "true when the command is accepted by the provider");

    FunctionSpec *set_led_fn = add_function_spec(caps, kFunctionSetLed, "set_led",
                                                 "Enable or disable the device status LED.", kCategoryConfig, true);
    add_arg_spec(set_led_fn, "enabled", kValueTypeBool, "LED state (true=on, false=off)", true);
    add_result_spec(set_led_fn, "enabled", kValueTypeBool, "Echoed requested LED state");
    add_result_spec(set_led_fn, "accepted", kValueTypeBool, "true when the command is accepted by the provider");

    FunctionSpec *sleep_fn = add_function_spec(caps, kFunctionSleep, "sleep",
                                               "Put the device into low-power sleep mode.", kCategoryConfig, true);
    add_result_spec(sleep_fn, "accepted", kValueTypeBool, "true when the command is accepted by the provider");
}

std::unique_ptr<i2c::ISession> make_session(const ProviderConfig &config) {
    if (is_mock_mode(config)) {
        return std::make_unique<i2c::NoopSession>(config.bus_path);
    }

#if defined(__linux__)
    return std::make_unique<i2c::LinuxSession>(config.bus_path, config.timeout_ms, config.retry_count);
#else
    return std::make_unique<i2c::NoopSession>(config.bus_path);
#endif
}

std::vector<ActiveDevice>::iterator find_active_device_unlocked(std::vector<ActiveDevice> &devices,
                                                                const std::string &device_id) {
    return std::find_if(devices.begin(), devices.end(),
                        [&device_id](const ActiveDevice &device) { return device.spec.id == device_id; });
}

anolis::deviceprovider::v1::CapabilitySet build_capabilities(const ProviderConfig &config, EzoDeviceType type) {
    anolis::deviceprovider::v1::CapabilitySet capabilities;
    size_t def_count = 0;
    const SignalDefinition *defs = signal_definitions_for_type(type, &def_count);
    for (size_t i = 0; i < def_count; ++i) {
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

// [§7.2] The curated default signal set for a device type — the is_default
// signals, returned when ReadSignalsRequest.signal_ids is empty.
std::vector<std::string> default_signal_ids_for_type(EzoDeviceType type) {
    size_t count = 0;
    const SignalDefinition *defs = signal_definitions_for_type(type, &count);
    std::vector<std::string> ids;
    for (size_t i = 0; i < count; ++i) {
        if (defs[i].is_default) {
            ids.emplace_back(defs[i].signal_id);
        }
    }
    return ids;
}

anolis::deviceprovider::v1::Device build_descriptor(const ProviderConfig &config, const DeviceSpec &spec) {
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
    switch (address) {
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
    if (info == nullptr) {
        return;
    }

    std::memset(info, 0, sizeof(*info));
    info->product_id = mock_product_for_address(address);

    const ezo_product_metadata_t *metadata = ezo_product_get_metadata(info->product_id);
    if (metadata != nullptr && metadata->vendor_short_code != nullptr) {
        std::snprintf(info->product_code, sizeof(info->product_code), "%s", metadata->vendor_short_code);
    } else {
        std::snprintf(info->product_code, sizeof(info->product_code), "UNK");
    }
    std::snprintf(info->firmware_version, sizeof(info->firmware_version), "mock-1.0");
}

i2c::Status probe_identity_real(i2c::BusExecutor &executor, const ProviderConfig &config, const DeviceSpec &spec,
                                ezo_device_info_t *info_out) {
    if (info_out == nullptr) {
        return make_status(i2c::StatusCode::InvalidArgument, "probe requires output info");
    }

    ezo_device_info_t info{};
    const ezo_product_id_t expected_product = expected_product_for_type(spec.type);
    const int timeout_ms = std::max(config.timeout_ms, 2000);

    const i2c::Status status =
        executor.submit("startup_probe:" + spec.id, std::chrono::milliseconds(timeout_ms), [&](i2c::ISession &session) {
            i2c::EzoDeviceBinding binding;
            i2c::Status bind_status = i2c::bind_ezo_i2c_device(session, static_cast<uint8_t>(spec.address), binding);
            if (!bind_status.is_ok()) {
                return bind_status;
            }

            ezo_timing_hint_t hint{};
            const ezo_result_t send_result = ezo_control_send_info_query_i2c(&binding.device, expected_product, &hint);
            if (send_result != EZO_OK) {
                return status_from_ezo_result(send_result, "send info query");
            }

            if (hint.wait_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(hint.wait_ms));
            }

            const ezo_result_t read_result = ezo_control_read_info_i2c(&binding.device, &info);
            if (read_result != EZO_OK) {
                return status_from_ezo_result(read_result, "read info response");
            }

            return i2c::Status::ok();
        });

    if (status.is_ok()) {
        *info_out = info;
    }
    return status;
}

i2c::Status read_sample_from_bound_device(ezo_i2c_device_t *device, const DeviceSpec &spec,
                                          std::vector<SignalSample> *signals_out) {
    if (device == nullptr || signals_out == nullptr) {
        return make_status(i2c::StatusCode::InvalidArgument, "device sample read requires valid pointers");
    }
    return devices::adapter_for(spec.type).read_sample(device, *signals_out);
}

void build_mock_sample(const DeviceSpec &spec, uint64_t sequence, std::vector<SignalSample> *signals_out) {
    if (signals_out == nullptr) {
        return;
    }
    devices::adapter_for(spec.type).build_mock_sample(spec.address, sequence, *signals_out);
}

std::string build_startup_message(const RuntimeState &state) {
    std::ostringstream out;
    out << "startup complete: active=" << state.active_devices.size() << ", excluded=" << state.excluded_devices.size()
        << ", configured=" << state.config.devices.size();
    if (!state.i2c_status_message.empty()) {
        out << ", i2c=" << state.i2c_status_message;
    }
    return out.str();
}

}  // namespace

void shutdown() {
    std::shared_ptr<i2c::BusExecutor> executor;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        executor = std::move(g_executor);
        g_state.i2c_executor_running = false;
    }
    if (executor) {
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
    if (!start_status.is_ok()) {
        state.startup_message = "startup failed to initialize I2C executor: " + start_status.message;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state = std::move(state);
        return;
    }

    // Device activation is all-or-some, not all-or-nothing. Each configured
    // device is probed independently so one bad address or family mismatch does
    // not hide the healthy devices on the same bus.
    const bool mock_mode = is_mock_mode(config);
    for (const DeviceSpec &spec : config.devices) {
        ezo_device_info_t info{};
        i2c::Status probe_status = i2c::Status::ok();
        if (mock_mode) {
            fill_mock_identity(spec.address, &info);
        } else {
            probe_status = probe_identity_real(*executor, config, spec, &info);
        }

        if (!probe_status.is_ok()) {
            state.excluded_devices.push_back(ExcludedDevice{spec, probe_status.message});
            continue;
        }

        const ezo_product_id_t expected_product = expected_product_for_type(spec.type);
        if (info.product_id != expected_product) {
            std::string actual = "unknown";
            if (const ezo_product_metadata_t *meta = ezo_product_get_metadata(info.product_id)) {
                actual = meta->family_name;
            }

            std::string expected = to_string(spec.type);
            state.excluded_devices.push_back(
                ExcludedDevice{spec, std::format("type mismatch: configured {}, detected {}", expected, actual)});
            continue;
        }

        ActiveDevice device;
        device.spec = spec;
        device.descriptor = build_descriptor(config, spec);
        device.capabilities = build_capabilities(config, spec.type);
        device.default_signal_ids = default_signal_ids_for_type(spec.type);
        device.startup_product_code = info.product_code;
        device.startup_firmware_version = info.firmware_version;
        device.sample.signals.resize(static_cast<size_t>(device.capabilities.signals_size()));
        (*device.descriptor.mutable_tags())["ezo_product_code"] = device.startup_product_code;
        (*device.descriptor.mutable_tags())["ezo_firmware"] = device.startup_firmware_version;
        state.active_devices.push_back(std::move(device));
    }

    state.ready = !state.active_devices.empty();
    if (state.ready) {
        state.ready_at = std::chrono::system_clock::now();
        if (state.i2c_status_message.empty()) {
            state.i2c_status_message = "ok";
        }
    }
    state.startup_message = build_startup_message(state);

    std::vector<std::string> active_ids;
    active_ids.reserve(state.active_devices.size());
    for (const ActiveDevice &device : state.active_devices) {
        active_ids.push_back(device.spec.id);
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state = std::move(state);
        g_executor = std::move(executor);
    }

    for (const std::string &device_id : active_ids) {
        const i2c::Status refresh_status = refresh_device_sample(device_id);
        if (!refresh_status.is_ok()) {
            logging::warning(
                std::format("startup sample failed for device '{}': {}", device_id, refresh_status.message));
        }
    }
}

RuntimeState snapshot() {
    // NOTE: this deep-copies the full RuntimeState (active_devices' proto
    // capabilities + sample vectors) under the global mutex per call. The
    // targeted shrink is deferred to the Wave-5 ezo->SDK migration, which
    // reworks this runtime anyway (see anolishq/anolis-protocol#44); the I2C
    // round-trip dominates the read path, so the copy is not the bottleneck.
    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeState copy = g_state;
    if (g_executor) {
        copy.i2c_executor_running = g_executor->is_running();
        copy.i2c_metrics = g_executor->snapshot_metrics();
        if (copy.i2c_metrics.last_error.empty()) {
            if (copy.i2c_status_message.empty()) {
                copy.i2c_status_message = "ok";
            }
        } else {
            copy.i2c_status_message = copy.i2c_metrics.last_error;
        }
    }
    return copy;
}

i2c::IoStats io_stats_for(uint8_t address) {
    std::shared_ptr<i2c::BusExecutor> executor;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        executor = g_executor;
    }
    if (!executor) {
        return i2c::IoStats{};
    }
    // Fetch the session once (each session() call takes the executor mutex).
    // The shared_ptr keeps the executor and its owned session alive for the
    // read; the stats accessor is internally synchronized.
    i2c::ISession *session = executor->session();
    return session->io_stats_for(address);
}

i2c::Status submit_i2c_job(const std::string &job_name, std::chrono::milliseconds timeout, i2c::BusExecutor::Job job) {
    std::shared_ptr<i2c::BusExecutor> executor;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        executor = g_executor;
    }

    if (!executor) {
        return i2c::Status{
            i2c::StatusCode::Unavailable,
            "i2c executor is not running",
        };
    }

    return executor->submit(job_name, timeout, std::move(job));
}

i2c::Status refresh_device_sample(const std::string &device_id) {
    if (device_id.empty()) {
        return make_status(i2c::StatusCode::InvalidArgument, "device_id is required");
    }

    ProviderConfig config;
    DeviceSpec spec;
    bool mock_mode = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = find_active_device_unlocked(g_state.active_devices, device_id);
        if (it == g_state.active_devices.end()) {
            return make_status(i2c::StatusCode::NotFound, "unknown device_id");
        }
        config = g_state.config;
        spec = it->spec;
        mock_mode = is_mock_mode(config);
    }

    if (mock_mode) {
        const auto now = std::chrono::system_clock::now();
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = find_active_device_unlocked(g_state.active_devices, device_id);
        if (it == g_state.active_devices.end()) {
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
    // Sampling is funneled through the shared executor so reads, identity
    // queries, and safe control calls all share one bus-serialization point.
    i2c::Status status =
        submit_i2c_job("sample:" + device_id, std::chrono::milliseconds(timeout_ms), [&](i2c::ISession &session) {
            i2c::EzoDeviceBinding binding;
            i2c::Status bind_status = i2c::bind_ezo_i2c_device(session, static_cast<uint8_t>(spec.address), binding);
            if (!bind_status.is_ok()) {
                return bind_status;
            }

            return read_sample_from_bound_device(&binding.device, spec, &new_signals);
        });

    const auto now = std::chrono::system_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = find_active_device_unlocked(g_state.active_devices, device_id);
        if (it == g_state.active_devices.end()) {
            return make_status(i2c::StatusCode::NotFound, "unknown device_id");
        }

        if (status.is_ok()) {
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

void record_call_result(const std::string &device_id, const std::string &function_name, bool ok,
                        const std::string &message) {
    if (device_id.empty()) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = find_active_device_unlocked(g_state.active_devices, device_id);
    if (it == g_state.active_devices.end()) {
        return;
    }

    it->has_last_call = true;
    it->last_call_ok = ok;
    it->last_call_function = function_name;
    it->last_call_at = now;

    if (ok) {
        it->last_call_error.clear();
        ++it->call_success_count;
    } else {
        it->last_call_error = message;
        ++it->call_failure_count;
    }
}

}  // namespace anolis_provider_ezo::runtime
