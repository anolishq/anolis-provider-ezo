/**
 * @file ezo_canned_bus.cpp
 * @brief Implementation of the canned EZO device bus.
 */

#include "i2c/ezo_canned_bus.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace anolis_provider_ezo::i2c {
namespace {

using anolis::provider_sdk::i2c::I2cError;
using anolis::provider_sdk::i2c::I2cStatus;
using anolis::provider_sdk::i2c::IoStats;

constexpr uint8_t kStatusSuccess = 0x01;  // EZO response code for SUCCESS.

// The address -> family map, mirroring the former mock_product_for_address so
// mock identity/type-match behaviour is unchanged.
CannedFamily family_for_address(uint8_t address) {
    switch (address) {
        case 0x61:
            return CannedFamily::Do;
        case 0x62:
            return CannedFamily::Orp;
        case 0x63:
            return CannedFamily::Ph;
        case 0x64:
            return CannedFamily::Ec;
        case 0x66:
            return CannedFamily::Rtd;
        case 0x6F:
            return CannedFamily::Hum;
        default:
            return CannedFamily::Unknown;
    }
}

// The former sample_helpers mock generators, moved to the wire.
double mock_base(uint8_t address) { return static_cast<double>((address % 17) + 1) * 0.1; }
double mock_delta(uint64_t sequence) { return static_cast<double>(sequence % 25) * 0.01; }

std::string fixed(double value, int decimals) {
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%.*f", decimals, value);
    return std::string(buf.data());
}

// `?I,<code>,mock-1.0` — the product code normalizes (strip '.', uppercase) to
// the registry code, so it matches the configured family exactly as the former
// fill_mock_identity did (which used the vendor short code + "mock-1.0").
std::string info_payload(CannedFamily family) {
    switch (family) {
        case CannedFamily::Ph:
            return "?I,pH,mock-1.0";
        case CannedFamily::Orp:
            return "?I,ORP,mock-1.0";
        case CannedFamily::Ec:
            return "?I,EC,mock-1.0";
        case CannedFamily::Do:
            return "?I,D.O.,mock-1.0";
        case CannedFamily::Rtd:
            return "?I,RTD,mock-1.0";
        case CannedFamily::Hum:
            return "?I,HUM,mock-1.0";
        case CannedFamily::Unknown:
            return "?I,UNK,mock-1.0";
    }
    return "?I,UNK,mock-1.0";
}

// `?O,<tokens>` — the enabled-output subset. Matches the former build_mock_sample
// which populated exactly these fields (DO: mg only; EC/HUM: first two), so the
// following `r` CSV width equals the enabled count (ezo_schema exact-count rule).
std::string output_config_payload(CannedFamily family) {
    switch (family) {
        case CannedFamily::Do:
            return "?O,mg";
        case CannedFamily::Ec:
            return "?O,EC,TDS";
        case CannedFamily::Hum:
            return "?O,HUM,T";
        default:
            return "";  // pH/ORP/RTD never issue an output query.
    }
}

// The `r` reading payload, reusing the former per-family build_mock_sample
// formulas so mock values stay in the same ranges. CSV width matches
// output_config_payload's enabled count.
std::string read_payload(CannedFamily family, uint8_t address, uint64_t seq) {
    const double base = mock_base(address);
    const double delta = mock_delta(seq);
    switch (family) {
        case CannedFamily::Ph:
            return fixed(6.5 + base + delta, 3);
        case CannedFamily::Orp:
            return fixed(250.0 + base * 10.0 + delta * 100.0, 1);
        case CannedFamily::Rtd:
            return fixed(20.0 + base + delta, 2);
        case CannedFamily::Do:
            return fixed(7.0 + base + delta, 2);
        case CannedFamily::Ec:
            return fixed(700.0 + base * 100.0 + delta * 100.0, 1) + "," + fixed(350.0 + base * 50.0 + delta * 80.0, 1);
        case CannedFamily::Hum:
            return fixed(45.0 + base * 5.0 + delta * 10.0, 1) + "," + fixed(22.0 + base + delta, 1);
        case CannedFamily::Unknown:
            return "";
    }
    return "";
}

bool starts_with(const std::string &s, const char *prefix) { return s.rfind(prefix, 0) == 0; }

}  // namespace

EzoCannedBus::EzoCannedBus(std::string bus_path) : bus_path_(std::move(bus_path)) {}

I2cStatus EzoCannedBus::open() {
    opened_ = true;
    return I2cStatus::ok();
}

void EzoCannedBus::close() { opened_ = false; }
bool EzoCannedBus::is_open() const { return opened_; }
const std::string &EzoCannedBus::bus_path() const { return bus_path_; }
void EzoCannedBus::delay_us(uint32_t) {}
IoStats EzoCannedBus::io_stats_for(uint8_t) const { return IoStats{}; }

EzoCannedBus::DeviceState &EzoCannedBus::state_for(uint8_t address) {
    auto it = devices_.find(address);
    if (it == devices_.end()) {
        DeviceState st;
        st.family = family_for_address(address);
        it = devices_.emplace(address, st).first;
    }
    return it->second;
}

std::string EzoCannedBus::response_payload(uint8_t address, DeviceState &state, const std::string &command) {
    if (command == "i") {
        return info_payload(state.family);
    }
    if (command == "O,?") {
        return output_config_payload(state.family);
    }
    if (command == "r" || starts_with(command, "rt,")) {
        return read_payload(state.family, address, state.read_seq++);
    }
    // Control sends (Find / L,1 / Sleep) and anything else are never read back.
    return "";
}

I2cStatus EzoCannedBus::emit_frame(const std::string &payload, uint8_t *rx_data, size_t rx_len, size_t *rx_received) {
    if (rx_data == nullptr || rx_len == 0) {
        return I2cStatus::failure(I2cError::InvalidArgument, "canned read requires rx buffer");
    }
    rx_data[0] = kStatusSuccess;
    size_t written = 1;
    for (size_t i = 0; i < payload.size() && written < rx_len; ++i, ++written) {
        rx_data[written] = static_cast<uint8_t>(payload[i]);
    }
    if (rx_received != nullptr) {
        *rx_received = written;
    }
    return I2cStatus::ok();
}

I2cStatus EzoCannedBus::write(uint8_t address, const uint8_t *tx_data, size_t tx_len) {
    if (!opened_) {
        return I2cStatus::failure(I2cError::NotOpen, "bus not open");
    }
    if (tx_len == 0) {
        return I2cStatus::failure(I2cError::InvalidArgument, "write requires tx_len>0");
    }
    state_for(address).last_command.assign(reinterpret_cast<const char *>(tx_data), tx_len);
    return I2cStatus::ok();
}

I2cStatus EzoCannedBus::read(uint8_t address, uint8_t *rx_data, size_t rx_len, size_t *rx_received, uint32_t) {
    if (!opened_) {
        return I2cStatus::failure(I2cError::NotOpen, "bus not open");
    }
    DeviceState &state = state_for(address);
    return emit_frame(response_payload(address, state, state.last_command), rx_data, rx_len, rx_received);
}

I2cStatus EzoCannedBus::write_then_read(uint8_t address, const uint8_t *tx_data, size_t tx_len, uint8_t *rx_data,
                                        size_t rx_len, size_t *rx_received) {
    if (!opened_) {
        return I2cStatus::failure(I2cError::NotOpen, "bus not open");
    }
    if (tx_len == 0 && rx_len == 0) {
        return I2cStatus::failure(I2cError::InvalidArgument, "write_then_read requires tx_len>0 or rx_len>0");
    }
    DeviceState &state = state_for(address);
    // The EZO driver splits command-write and response-read into separate calls
    // (write: tx>0,rx=0; read: tx=0,rx>0), but handle the atomic form too.
    if (tx_len > 0) {
        state.last_command.assign(reinterpret_cast<const char *>(tx_data), tx_len);
    }
    if (rx_len == 0) {
        if (rx_received != nullptr) {
            *rx_received = 0;
        }
        return I2cStatus::ok();
    }
    return emit_frame(response_payload(address, state, state.last_command), rx_data, rx_len, rx_received);
}

}  // namespace anolis_provider_ezo::i2c
