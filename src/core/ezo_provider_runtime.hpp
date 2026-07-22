#pragma once

/**
 * @file ezo_provider_runtime.hpp
 * @brief ezo's implementation of the shared SDK `ProviderRuntime` seam.
 *
 * Thin, stateless adapter over the `runtime::` process singleton: each method
 * delegates to `runtime::snapshot()` + the device/executor substrate. The SDK's
 * generic run-loop + handlers own all ADPP envelope/policy. `read()` performs the
 * D1 conversion (the cached per-family `SignalSample` -> proto `SignalValue` with
 * `quality_from`); the shared I2C bus seam (`I2cBus`/`BusExecutor`/
 * `bind_ezo_i2c_device`) stays entirely inside `runtime_state.cpp` — the SDK never
 * sees it. ezo keeps its own `DeviceAdapter` descriptor (over `ezo_i2c_device_t*`)
 * and `adapter_for` taxonomy; it does not instantiate the SDK's `DeviceAdapter<HandleT>`.
 */

#include "anolis/provider_sdk/runtime.hpp"

namespace anolis_provider_ezo {

class EzoProviderRuntime : public anolis::provider_sdk::ProviderRuntime {
public:
    anolis::provider_sdk::ProviderMetadata metadata() const override;
    anolis::provider_sdk::ReadinessReport readiness() const override;
    anolis::provider_sdk::DeviceHealthExtra device_health(const std::string& device_id) const override;
    std::vector<std::string> list_device_ids() const override;
    bool has_device(const std::string& device_id) const override;
    anolis::deviceprovider::v1::Device device_info(const std::string& device_id) const override;
    anolis::deviceprovider::v1::CapabilitySet capabilities(const std::string& device_id) const override;
    anolis::provider_sdk::AdapterReadResult read(const std::string& device_id,
                                                 const std::vector<std::string>& signal_ids) override;
    anolis::provider_sdk::AdapterCallResult call(const std::string& device_id, uint32_t function_id,
                                                 const anolis::provider_sdk::ValueMap& args) override;
    std::optional<uint32_t> resolve_function_id(const std::string& device_id,
                                                const std::string& function_name) const override;
};

}  // namespace anolis_provider_ezo
