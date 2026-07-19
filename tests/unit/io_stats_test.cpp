#include <gtest/gtest.h>

#include "i2c/session.hpp"

namespace {

using anolis_provider_ezo::i2c::IoStats;
using anolis_provider_ezo::i2c::IoStatsMap;
using anolis_provider_ezo::i2c::NoopSession;

TEST(IoStatsMapTest, UnseenAddressReturnsZeros) {
    const IoStatsMap stats;
    const IoStats s = stats.stats_for(0x63);
    EXPECT_EQ(s.ok, 0u);
    EXPECT_EQ(s.failed, 0u);
    EXPECT_EQ(s.retried_attempts, 0u);
}

TEST(IoStatsMapTest, FirstAttemptSuccessCountsOkOnly) {
    IoStatsMap stats;
    stats.record(0x63, true, 1);
    const IoStats s = stats.stats_for(0x63);
    EXPECT_EQ(s.ok, 1u);
    EXPECT_EQ(s.failed, 0u);
    EXPECT_EQ(s.retried_attempts, 0u);
}

TEST(IoStatsMapTest, MaskedRetryStaysVisible) {
    // An operation that succeeds on its third attempt is one ok plus two
    // retried attempts — the masked-retry property (ezo#100).
    IoStatsMap stats;
    stats.record(0x63, true, 3);
    const IoStats s = stats.stats_for(0x63);
    EXPECT_EQ(s.ok, 1u);
    EXPECT_EQ(s.failed, 0u);
    EXPECT_EQ(s.retried_attempts, 2u);
}

TEST(IoStatsMapTest, ExhaustionCountsOneFailurePlusRetries) {
    IoStatsMap stats;
    stats.record(0x61, false, 3);
    const IoStats s = stats.stats_for(0x61);
    EXPECT_EQ(s.ok, 0u);
    EXPECT_EQ(s.failed, 1u);
    EXPECT_EQ(s.retried_attempts, 2u);
}

TEST(IoStatsMapTest, StatsAreTrackedPerAddress) {
    IoStatsMap stats;
    stats.record(0x61, true, 1);
    stats.record(0x63, false, 2);
    EXPECT_EQ(stats.stats_for(0x61).ok, 1u);
    EXPECT_EQ(stats.stats_for(0x61).failed, 0u);
    EXPECT_EQ(stats.stats_for(0x63).ok, 0u);
    EXPECT_EQ(stats.stats_for(0x63).failed, 1u);
    EXPECT_EQ(stats.stats_for(0x63).retried_attempts, 1u);
}

TEST(IoStatsMapTest, CountsAccumulateAcrossOperations) {
    IoStatsMap stats;
    stats.record(0x63, true, 1);
    stats.record(0x63, true, 2);
    stats.record(0x63, false, 3);
    const IoStats s = stats.stats_for(0x63);
    EXPECT_EQ(s.ok, 2u);
    EXPECT_EQ(s.failed, 1u);
    EXPECT_EQ(s.retried_attempts, 3u);
}

TEST(IoStatsTest, NoopSessionReportsZeros) {
    // NoopSession carries no traffic; the ISession default returns zeros so
    // mock builds surface the io_* keys honestly at zero.
    NoopSession session("mock://io-stats");
    const IoStats s = session.io_stats_for(0x63);
    EXPECT_EQ(s.ok, 0u);
    EXPECT_EQ(s.failed, 0u);
    EXPECT_EQ(s.retried_attempts, 0u);
}

}  // namespace
