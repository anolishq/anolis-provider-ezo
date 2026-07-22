#pragma once

/**
 * @file sample_helpers.hpp
 * @brief Family-agnostic helpers shared by the per-family device-adapter
 * modules: signal-slot mutation, EZO error mapping, timing waits, and the
 * deterministic mock value generators.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "devices/common/signal_definition.hpp"
#include "devices/common/signal_sample.hpp"
#include "i2c/status.hpp"

extern "C" {
#include "ezo_control.h"
#include "ezo_i2c.h"
#include "ezo_product.h"
}

namespace anolis_provider_ezo::devices {

/** @brief Construct an i2c::Status with the given code and message. */
i2c::Status make_status(i2c::StatusCode code, const std::string &message);

/**
 * @brief Wait the device-suggested settle time between command and read.
 *
 * Routed through the device's bus (`delay_us`) rather than sleeping directly:
 * the real `LinuxI2cBus` sleeps (hardware settle preserved), while the mock
 * canned bus no-ops (instant, so mock/CI does not pay real device timings).
 */
void wait_for_timing_hint(ezo_i2c_device_t *device, const ezo_timing_hint_t &hint);

/** @brief Map an ezo_result_t onto the provider's i2c::Status vocabulary. */
i2c::Status status_from_ezo_result(ezo_result_t result, const std::string &context);

/** @brief Mark a signal slot as available and carrying a value. */
void set_signal_value(std::vector<SignalSample> &signals, std::size_t index, double value);

/** @brief Mark a signal slot as unavailable with a reason. */
void set_signal_unavailable(std::vector<SignalSample> &signals, std::size_t index, const std::string &reason);

/** @brief Reset the output vector to one empty slot per declared signal. */
void initialize_signal_samples(const SignalDefinition *defs, std::size_t count, std::vector<SignalSample> &signals_out);

}  // namespace anolis_provider_ezo::devices
