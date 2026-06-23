#pragma once

/**
 * @file signal_definition.hpp
 * @brief Static description of one device signal, shared by the per-family
 * device-adapter modules.
 */

namespace anolis_provider_ezo::devices {

struct SignalDefinition {
    const char *signal_id;
    const char *name;
    const char *description;
    const char *unit;
    // [§7.2] Included in the default signal set returned for an empty
    // ReadSignalsRequest.signal_ids — the primary, routinely-useful telemetry.
    // Derived/specialized signals are excluded from the default.
    bool is_default;
};

}  // namespace anolis_provider_ezo::devices
