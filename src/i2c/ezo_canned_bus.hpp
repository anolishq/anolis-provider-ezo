#pragma once

/**
 * @file ezo_canned_bus.hpp
 * @brief A canned EZO device on the shared I2C bus, for mock mode.
 *
 * The Level-2 mock (anolishq/anolis-provider-sdk#19): instead of synthesizing
 * identity and samples *above* the bus (the removed `NoopI2cBus` +
 * `fill_mock_identity`/`build_mock_sample` path), this bus answers the EZO C
 * driver's real command/response protocol *through* the transport, so mock mode
 * exercises the same driver command + parse path as real hardware. Wrapped in
 * `FaultInjectingI2cBus`, it also gives always-on fault-injection coverage
 * (anolishq/anolis#99) — the mock analog of the bread#97 blind spot.
 *
 * It is a stateful per-address responder mirroring the EZO I2C wire protocol:
 *   - a write (tx>0, rx=0) records the ASCII command for that address;
 *   - a read (tx=0, rx>0) emits `[0x01][ASCII payload]` (status = SUCCESS) for
 *     the last-recorded command, with `rx_received = 1 + payload_len`.
 * The address → family map mirrors the former `mock_product_for_address`, and
 * the read values reuse the former `mock_base`/`mock_delta` generators, so mock
 * readings stay in the same plausible, per-address-varying ranges.
 *
 * Control commands (Find / L,1 / Sleep) are send-only in this provider (no
 * status frame is read back), so the bus only has to accept the write.
 */

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "anolis/provider_sdk/i2c/i2c_bus.hpp"
#include "anolis/provider_sdk/i2c/i2c_status.hpp"
#include "anolis/provider_sdk/i2c/io_stats.hpp"

namespace anolis_provider_ezo::i2c {

/** @brief The EZO families this canned bus can impersonate, by I2C address. */
enum class CannedFamily { Unknown, Ph, Orp, Ec, Do, Rtd, Hum };

class EzoCannedBus final : public anolis::provider_sdk::i2c::I2cBus {
public:
    explicit EzoCannedBus(std::string bus_path);

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
    struct DeviceState {
        CannedFamily family = CannedFamily::Unknown;
        std::string last_command;
        uint64_t read_seq = 0;
    };

    DeviceState &state_for(uint8_t address);
    // Build the ASCII payload a real device would return for `command`, or empty
    // if the command expects no readable response (e.g. control sends).
    std::string response_payload(uint8_t address, DeviceState &state, const std::string &command);
    // Copy `[0x01][payload]` into rx (truncated to rx_len) and set rx_received.
    static anolis::provider_sdk::i2c::I2cStatus emit_frame(const std::string &payload, uint8_t *rx_data, size_t rx_len,
                                                           size_t *rx_received);

    std::string bus_path_;
    bool opened_ = false;
    std::map<uint8_t, DeviceState> devices_;
};

}  // namespace anolis_provider_ezo::i2c
