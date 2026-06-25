#pragma once

/**
 * @file signal_quality.hpp
 * @brief Derive an ADPP `SignalValue.Quality` from a reading's availability and
 * the device's last-read freshness.
 *
 * Lifted out of the read handler so the one quality rule applies wherever a
 * `SignalValue` is built: the wire boundary today, the device adapter once the
 * read path returns `SignalValue` directly, and the shared provider SDK. Lives
 * in the device layer; it depends only on the ADPP contract (proto), never on
 * `core/` or `config/`.
 */

#include <cstdint>

#include "protocol.pb.h"

namespace anolis_provider_ezo::devices {

/**
 * @brief Map a reading's flags + age onto an ADPP quality.
 *
 * @param available       the adapter produced a usable sample slot for the signal
 * @param has_value       the slot carries a concrete value
 * @param last_read_ok    the device's most recent bus read succeeded
 * @param age_ms          age of the sample (now - sampled_at), milliseconds
 * @param stale_after_ms  staleness threshold, milliseconds
 *
 * Precondition: the caller has a sample for the signal; a missing sample is the
 * caller's `QUALITY_UNKNOWN` case.
 */
inline anolis::deviceprovider::v1::SignalValue::Quality quality_from(bool available, bool has_value, bool last_read_ok,
                                                                     std::int64_t age_ms, std::int64_t stale_after_ms) {
    using SignalValue = anolis::deviceprovider::v1::SignalValue;
    if (!available) {
        return SignalValue::QUALITY_UNKNOWN;
    }
    if (!last_read_ok) {
        return SignalValue::QUALITY_FAULT;
    }
    if (age_ms > stale_after_ms) {
        return SignalValue::QUALITY_STALE;
    }
    if (has_value) {
        return SignalValue::QUALITY_OK;
    }
    return SignalValue::QUALITY_UNKNOWN;
}

}  // namespace anolis_provider_ezo::devices
