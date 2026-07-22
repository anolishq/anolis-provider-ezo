#include "i2c/ezo_canned_bus.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "anolis/provider_sdk/i2c/fault_injecting_i2c_bus.hpp"

// The canned bus answers the EZO driver's wire protocol so mock mode runs the
// real command/parse path, and — wrapped in the SDK FaultInjectingI2cBus — gives
// always-on fault-injection coverage (anolishq/anolis#99). These tests exercise
// the wire contract directly and the fault decorator over it.

namespace {

using anolis_provider_ezo::i2c::EzoCannedBus;
namespace sdk = anolis::provider_sdk::i2c;

// One command/response exchange as the driver performs it: a write-only command
// followed by a read-only response. Returns the ASCII payload (frame after the
// leading 0x01 status byte).
std::string exchange(sdk::I2cBus& bus, uint8_t addr, const std::string& cmd) {
    EXPECT_TRUE(static_cast<bool>(bus.write(addr, reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size())));
    std::vector<uint8_t> rx(64, 0);
    size_t received = 0;
    EXPECT_TRUE(static_cast<bool>(bus.read(addr, rx.data(), rx.size(), &received, 0)));
    EXPECT_GE(received, 1U);
    EXPECT_EQ(rx[0], 0x01);  // EZO SUCCESS status byte
    return std::string(reinterpret_cast<char*>(rx.data() + 1), received - 1);
}

TEST(EzoCannedBusTest, InfoQueryReturnsFamilyPerAddress) {
    EzoCannedBus bus("mock://canned");
    ASSERT_TRUE(static_cast<bool>(bus.open()));

    // The `?I,<code>,<fw>` frame the driver's ezo_parse_device_info accepts; the
    // code normalizes (strip '.', uppercase) to the registry family.
    EXPECT_EQ(exchange(bus, 0x63, "i"), "?I,pH,mock-1.0");
    EXPECT_EQ(exchange(bus, 0x61, "i"), "?I,D.O.,mock-1.0");
    EXPECT_EQ(exchange(bus, 0x62, "i"), "?I,ORP,mock-1.0");
    EXPECT_EQ(exchange(bus, 0x64, "i"), "?I,EC,mock-1.0");
    EXPECT_EQ(exchange(bus, 0x66, "i"), "?I,RTD,mock-1.0");
    EXPECT_EQ(exchange(bus, 0x6F, "i"), "?I,HUM,mock-1.0");
    // Unmapped address -> UNKNOWN (excluded at identity check), mirroring the old
    // mock_product_for_address default.
    EXPECT_EQ(exchange(bus, 0x10, "i"), "?I,UNK,mock-1.0");
}

TEST(EzoCannedBusTest, ScalarReadIsAParseableFloat) {
    EzoCannedBus bus("mock://canned");
    ASSERT_TRUE(static_cast<bool>(bus.open()));

    for (uint8_t addr : {0x63, 0x62, 0x66}) {  // pH, ORP, RTD are single-field
        const std::string payload = exchange(bus, addr, "r");
        EXPECT_EQ(payload.find(','), std::string::npos) << "single field for " << int(addr);
        EXPECT_NO_THROW((void)std::stod(payload)) << payload;
    }
}

TEST(EzoCannedBusTest, MultiFieldOutputConfigMatchesReadWidth) {
    EzoCannedBus bus("mock://canned");
    ASSERT_TRUE(static_cast<bool>(bus.open()));

    // DO: one enabled output (mg) -> 1 read field. The exact-count rule in
    // ezo_schema requires the read CSV width to equal the O,? token count.
    EXPECT_EQ(exchange(bus, 0x61, "O,?"), "?O,mg");
    EXPECT_EQ(exchange(bus, 0x61, "r").find(','), std::string::npos);

    // EC / HUM: two enabled outputs -> 2 read fields (one comma).
    EXPECT_EQ(exchange(bus, 0x64, "O,?"), "?O,EC,TDS");
    const std::string ec_read = exchange(bus, 0x64, "r");
    EXPECT_EQ(std::count(ec_read.begin(), ec_read.end(), ','), 1);
    EXPECT_EQ(exchange(bus, 0x6F, "O,?"), "?O,HUM,T");
    const std::string hum_read = exchange(bus, 0x6F, "r");
    EXPECT_EQ(std::count(hum_read.begin(), hum_read.end(), ','), 1);
}

TEST(EzoCannedBusTest, WriteThenReadServesTheBridgePath) {
    // The real ezo_i2c_bridge drives the bus ONLY through write_then_read: a
    // command write (rx_len=0) then a response read (tx_len=0). Exercise that
    // exact shape rather than the split write()/read() the helper uses.
    EzoCannedBus bus("mock://canned");
    ASSERT_TRUE(static_cast<bool>(bus.open()));

    const std::string cmd = "i";
    size_t received = 0;
    ASSERT_TRUE(static_cast<bool>(
        bus.write_then_read(0x63, reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size(), nullptr, 0, &received)));

    std::vector<uint8_t> rx(64, 0);
    ASSERT_TRUE(static_cast<bool>(bus.write_then_read(0x63, nullptr, 0, rx.data(), rx.size(), &received)));
    ASSERT_GE(received, 1U);
    EXPECT_EQ(rx[0], 0x01);
    EXPECT_EQ(std::string(reinterpret_cast<char*>(rx.data() + 1), received - 1), "?I,pH,mock-1.0");
}

TEST(EzoCannedBusTest, ReadValuesVaryAcrossReads) {
    EzoCannedBus bus("mock://canned");
    ASSERT_TRUE(static_cast<bool>(bus.open()));
    // The ported mock_delta jitter makes successive reads differ (not frozen).
    std::string first = exchange(bus, 0x63, "r");
    std::string second = exchange(bus, 0x63, "r");
    EXPECT_NE(first, second);
}

// --- Fault injection over the canned bus (the #99 coverage win) ---

TEST(EzoFaultInjectionTest, InjectedReadFailCountsIoFailed) {
    auto canned = std::make_unique<EzoCannedBus>("mock://canned");
    sdk::FaultSpec spec;
    spec.read_fail_every = 1;  // every read phase fails at the transport
    sdk::FaultInjectingI2cBus bus(std::move(canned), spec);
    ASSERT_TRUE(static_cast<bool>(bus.open()));

    const std::string cmd = "i";
    EXPECT_TRUE(static_cast<bool>(bus.write(0x63, reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size())));
    std::vector<uint8_t> rx(64, 0);
    size_t received = 0;
    const auto status = bus.read(0x63, rx.data(), rx.size(), &received, 0);

    EXPECT_FALSE(static_cast<bool>(status));       // read failed as injected
    EXPECT_GE(bus.io_stats_for(0x63).failed, 1U);  // and it is counted io_failed
}

TEST(EzoFaultInjectionTest, CorruptStaysIoOkButBreaksTheFrame) {
    // A clean reference frame.
    EzoCannedBus clean("mock://canned");
    ASSERT_TRUE(static_cast<bool>(clean.open()));
    const std::string clean_payload = exchange(clean, 0x63, "i");

    // corrupt_every=1 mutates a successful read's bytes: the transport succeeded,
    // so it stays io_ok (docs/metrics.md) and surfaces as a decode failure above.
    auto canned = std::make_unique<EzoCannedBus>("mock://canned");
    sdk::FaultSpec spec;
    spec.corrupt_every = 1;
    sdk::FaultInjectingI2cBus bus(std::move(canned), spec);
    ASSERT_TRUE(static_cast<bool>(bus.open()));

    const std::string cmd = "i";
    ASSERT_TRUE(static_cast<bool>(bus.write(0x63, reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size())));
    std::vector<uint8_t> rx(64, 0);
    size_t received = 0;
    const auto status = bus.read(0x63, rx.data(), rx.size(), &received, 0);

    ASSERT_TRUE(static_cast<bool>(status));  // transport succeeded
    // A mutation is NOT a transport failure: no injected io_failed. (The canned
    // bus itself reports zero io — synthetic device — so mock io_* stays 0 as
    // documented; only injected transport faults surface as io_failed.)
    EXPECT_EQ(bus.io_stats_for(0x63).failed, 0U);
    ASSERT_GE(received, 1U);
    const std::string got(reinterpret_cast<char*>(rx.data() + 1), received - 1);
    // The status byte or a payload byte was mutated, so it no longer matches the
    // clean frame the driver would parse (here corrupt flips the status byte).
    EXPECT_TRUE(rx[0] != 0x01 || got != clean_payload);
}

}  // namespace
