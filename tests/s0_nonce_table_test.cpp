#include "NonceTable.hpp"

#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

namespace
{
using Clock = S0::NonceTable::Clock;
constexpr Clock::time_point T0{};
constexpr std::uint8_t PEER = 7;
}  // namespace

TEST(S0NonceTable, GenerateThenTakeReturnsSameNonceOnce)
{
    S0::NonceTable table;
    const auto nonce = table.generate(PEER, T0);
    EXPECT_EQ(table.size(), 1U);

    const auto taken = table.take(PEER, nonce[0], T0);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, nonce);
    EXPECT_EQ(table.size(), 0U);  // consumed
}

TEST(S0NonceTable, TakeIsSingleUse)
{
    S0::NonceTable table;
    const auto nonce = table.generate(PEER, T0);
    EXPECT_TRUE(table.take(PEER, nonce[0], T0).has_value());
    EXPECT_FALSE(table.take(PEER, nonce[0], T0).has_value());  // already consumed
}

TEST(S0NonceTable, UnknownIdReturnsNullopt)
{
    S0::NonceTable table;
    const auto nonce = table.generate(PEER, T0);
    EXPECT_FALSE(table.take(PEER, static_cast<std::uint8_t>(nonce[0] + 1), T0).has_value());
}

TEST(S0NonceTable, PerPeerIsolation)
{
    S0::NonceTable table;
    const auto nonce = table.generate(PEER, T0);
    EXPECT_FALSE(table.take(PEER + 1, nonce[0], T0).has_value());
    EXPECT_TRUE(table.take(PEER, nonce[0], T0).has_value());  // still there for the right peer
}

TEST(S0NonceTable, ExpiredNonceRejected)
{
    S0::NonceTable table(std::chrono::seconds(10));
    const auto nonce = table.generate(PEER, T0);
    EXPECT_FALSE(table.take(PEER, nonce[0], T0 + std::chrono::seconds(11)).has_value());
}

TEST(S0NonceTable, FreshnessBoundaryIsInclusive)
{
    S0::NonceTable table(std::chrono::seconds(10));
    const auto nonce = table.generate(PEER, T0);
    // Exactly at the window edge the nonce is still valid (expiry is strictly >).
    EXPECT_TRUE(table.take(PEER, nonce[0], T0 + std::chrono::seconds(10)).has_value());
}

TEST(S0NonceTable, PurgeExpiredDropsOnlyStale)
{
    S0::NonceTable table(std::chrono::seconds(10));
    table.generate(PEER, T0);                                // age 11s at purge → stale
    table.generate(PEER + 1, T0 + std::chrono::seconds(5));  // age 6s at purge → kept
    EXPECT_EQ(table.size(), 2U);
    table.purgeExpired(T0 + std::chrono::seconds(11));
    EXPECT_EQ(table.size(), 1U);
}
