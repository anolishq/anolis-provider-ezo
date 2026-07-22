#pragma once

/**
 * @file noop_i2c_bus.hpp
 * @brief No-hardware I2cBus for non-Linux (no i2c-dev) hardware builds.
 *
 * open() succeeds so the serialized executor still starts and the io_* keys
 * surface honestly at zero, but any actual transaction reports the transport as
 * unavailable — there is no faked data. Mock mode does NOT use this bus: it runs
 * the real EZO command/parse path against the fault-injectable EzoCannedBus
 * (anolishq/anolis-provider-sdk#19).
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include "anolis/provider_sdk/i2c/i2c_bus.hpp"
#include "anolis/provider_sdk/i2c/i2c_status.hpp"
#include "anolis/provider_sdk/i2c/io_stats.hpp"

namespace anolis_provider_ezo::i2c {

/**
 * @brief I2cBus implementation that performs no hardware I/O.
 */
class NoopI2cBus final : public anolis::provider_sdk::i2c::I2cBus {
public:
    explicit NoopI2cBus(std::string bus_path);

    anolis::provider_sdk::i2c::I2cStatus open() override;
    void close() override;
    bool is_open() const override;
    const std::string &bus_path() const override;

    anolis::provider_sdk::i2c::I2cStatus write(uint8_t address, const uint8_t *tx_data, size_t tx_len) override;
    anolis::provider_sdk::i2c::I2cStatus read(uint8_t address, uint8_t *rx_data, size_t rx_len, size_t *rx_received,
                                              uint32_t timeout_us) override;
    anolis::provider_sdk::i2c::I2cStatus write_then_read(uint8_t address, const uint8_t *tx_data, size_t tx_len,
                                                         uint8_t *rx_data, size_t rx_len, size_t *rx_received) override;
    void delay_us(uint32_t delay_us) override;
    anolis::provider_sdk::i2c::IoStats io_stats_for(uint8_t address) const override;

private:
    std::string bus_path_;
    bool opened_ = false;
};

}  // namespace anolis_provider_ezo::i2c
