#pragma once

/**
 * @file signal_sample.hpp
 * @brief The per-signal read result produced by a device adapter.
 *
 * Lives in the device layer (not core) so the adapter interface is
 * self-contained: `core` depends on `devices`, never the reverse. The
 * runtime aliases this type (`SignalSample`) for its sample cache.
 */

#include <string>

namespace anolis_provider_ezo::devices {

struct SignalSample {
    bool available = true;
    bool has_value = false;
    double value = 0.0;
    std::string unavailable_reason;
};

}  // namespace anolis_provider_ezo::devices
