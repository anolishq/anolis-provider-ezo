/**
 * @file noop_i2c_bus.cpp
 * @brief Implementation of the no-hardware I2cBus.
 */

#include "i2c/noop_i2c_bus.hpp"

#include <utility>

namespace anolis_provider_ezo::i2c {

using anolis::provider_sdk::i2c::I2cError;
using anolis::provider_sdk::i2c::I2cStatus;
using anolis::provider_sdk::i2c::IoStats;

NoopI2cBus::NoopI2cBus(std::string bus_path) : bus_path_(std::move(bus_path)) {}

I2cStatus NoopI2cBus::open() {
    opened_ = true;
    return I2cStatus::ok();
}

void NoopI2cBus::close() { opened_ = false; }

bool NoopI2cBus::is_open() const { return opened_; }

const std::string &NoopI2cBus::bus_path() const { return bus_path_; }

I2cStatus NoopI2cBus::write(uint8_t, const uint8_t *, size_t) {
    if (!opened_) {
        return I2cStatus::failure(I2cError::NotOpen, "bus not open");
    }
    return I2cStatus::failure(I2cError::BusError, "hardware integration disabled in this build");
}

I2cStatus NoopI2cBus::read(uint8_t, uint8_t *, size_t, size_t *rx_received, uint32_t) {
    if (rx_received != nullptr) {
        *rx_received = 0;
    }
    if (!opened_) {
        return I2cStatus::failure(I2cError::NotOpen, "bus not open");
    }
    return I2cStatus::failure(I2cError::BusError, "hardware integration disabled in this build");
}

I2cStatus NoopI2cBus::write_then_read(uint8_t, const uint8_t *, size_t, uint8_t *, size_t, size_t *rx_received) {
    if (rx_received != nullptr) {
        *rx_received = 0;
    }
    if (!opened_) {
        return I2cStatus::failure(I2cError::NotOpen, "bus not open");
    }
    return I2cStatus::failure(I2cError::BusError, "hardware integration disabled in this build");
}

void NoopI2cBus::delay_us(uint32_t) {}

IoStats NoopI2cBus::io_stats_for(uint8_t) const { return IoStats{}; }

}  // namespace anolis_provider_ezo::i2c
