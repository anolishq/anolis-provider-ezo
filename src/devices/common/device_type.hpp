#pragma once

/**
 * @file device_type.hpp
 * @brief The closed set of EZO device families.
 *
 * Lives in the devices layer (not `config/`) so the device-adapter descriptor
 * can name the taxonomy without depending on provider configuration. This keeps
 * the dependency edge `config → devices` (config parses into the taxonomy),
 * never `devices → config`, which is the direction the Wave-5 SDK extraction
 * requires (`provider → SDK → contract`). The enum stays provider-local; only
 * the descriptor + helpers are SDK-bound.
 */

namespace anolis_provider_ezo {

/**
 * @brief Supported EZO device families with fixed signal/function surfaces.
 */
enum class EzoDeviceType {
    Ph,
    Orp,
    Ec,
    Do,
    Rtd,
    Hum,
};

}  // namespace anolis_provider_ezo
