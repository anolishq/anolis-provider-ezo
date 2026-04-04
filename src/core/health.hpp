#pragma once

/**
 * @file health.hpp
 * @brief Health and readiness projection helpers for the EZO provider runtime state.
 */

#include <vector>

#include "core/runtime_state.hpp"
#include "protocol.pb.h"

namespace anolis_provider_ezo::health {

using DeviceHealth = anolis::deviceprovider::v1::DeviceHealth;
using ProviderHealth = anolis::deviceprovider::v1::ProviderHealth;
using WaitReadyResponse = anolis::deviceprovider::v1::WaitReadyResponse;

/** @brief Build provider-level health from the current runtime snapshot. */
ProviderHealth make_provider_health(const runtime::RuntimeState &state);

/** @brief Build per-device health views from the current runtime snapshot. */
std::vector<DeviceHealth> make_device_health(const runtime::RuntimeState &state,
                                             bool include_excluded = true);

/** @brief Populate the ADPP `WaitReady` response from runtime state. */
void populate_wait_ready(const runtime::RuntimeState &state, WaitReadyResponse &out);

} // namespace anolis_provider_ezo::health
