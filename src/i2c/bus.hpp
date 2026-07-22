#pragma once

/**
 * @file bus.hpp
 * @brief Adoption of the shared SDK I2C bus seam into the ezo provider.
 *
 * ezo's former `ISession`/`LinuxSession`/`IoStats` abstraction is replaced by
 * the protocol-agnostic raw-byte transport in anolis-provider-sdk
 * (anolishq/anolis-provider-sdk#19). This header pulls the SDK types into the
 * `anolis_provider_ezo::i2c` namespace under their existing names so provider
 * call sites keep referring to `i2c::I2cBus` / `i2c::IoStats`, and defines the
 * one boundary mapping from the transport `I2cStatus` to the provider op
 * `Status`.
 */

#include "anolis/provider_sdk/i2c/i2c_bus.hpp"
#include "anolis/provider_sdk/i2c/i2c_status.hpp"
#include "anolis/provider_sdk/i2c/io_stats.hpp"
#include "i2c/status.hpp"

namespace anolis_provider_ezo::i2c {

/** @brief The shared raw-byte I2C transport the provider serializes access to. */
using I2cBus = anolis::provider_sdk::i2c::I2cBus;

/** @brief Transport-level status reported by the bus. */
using I2cStatus = anolis::provider_sdk::i2c::I2cStatus;
using I2cError = anolis::provider_sdk::i2c::I2cError;

/** @brief Per-address transport I/O counters (io_ok/io_failed/io_retried). */
using IoStats = anolis::provider_sdk::i2c::IoStats;
using IoStatsMap = anolis::provider_sdk::i2c::IoStatsMap;

/**
 * @brief Map a transport `I2cStatus` onto the provider op `Status`.
 *
 * The bus speaks a protocol-agnostic transport vocabulary; the provider surface
 * (executor, health) speaks the broader op vocabulary. This is the single seam
 * where the two meet — used when the executor opens the bus. (Per-transaction
 * transport results reach the EZO driver as its own `ezo_result_t` through the
 * bridge, so they do not pass through here.)
 */
inline Status status_from_i2c(const I2cStatus &status) {
    if (status) {
        return Status::ok();
    }
    StatusCode code = StatusCode::Unavailable;
    switch (status.code) {
        case I2cError::InvalidArgument:
            code = StatusCode::InvalidArgument;
            break;
        case I2cError::Timeout:
            code = StatusCode::DeadlineExceeded;
            break;
        case I2cError::Ok:
        case I2cError::NotOpen:
        case I2cError::OpenFailed:
        case I2cError::WriteFailed:
        case I2cError::ReadFailed:
        case I2cError::BusError:
            code = StatusCode::Unavailable;
            break;
    }
    return Status{code, status.message};
}

}  // namespace anolis_provider_ezo::i2c
