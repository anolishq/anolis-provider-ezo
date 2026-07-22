#include <gtest/gtest.h>

#include "i2c/bus.hpp"
#include "i2c/noop_i2c_bus.hpp"

// The per-address IoStats accounting (record/stats_for, the masked-retry
// property from ezo#100) now lives in the shared SDK bus module and is
// exercised by anolis-provider-sdk's i2c_test.cpp. ezo consumes those types via
// the `i2c::IoStats` alias, so the only ezo-specific contract left to guard here
// is that the mock (no-hardware) bus surfaces the io_* keys honestly at zero.

namespace {

using anolis_provider_ezo::i2c::IoStats;
using anolis_provider_ezo::i2c::NoopI2cBus;

TEST(IoStatsTest, NoopI2cBusReportsZeros) {
    NoopI2cBus bus("mock://io-stats");
    const IoStats s = bus.io_stats_for(0x63);
    EXPECT_EQ(s.ok, 0u);
    EXPECT_EQ(s.failed, 0u);
    EXPECT_EQ(s.retried_attempts, 0u);
}

}  // namespace
