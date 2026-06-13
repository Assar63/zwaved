// Security S2 (CC 0x9F) SPAN — CTR_DRBG nonce protocol (#181). No published
// spec vectors, so these pin internal consistency: determinism (both peers
// agree), advance, serialize/resume, counter rollover, and desync→resync.

#include "Span.hpp"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace
{
const S2::SPAN::EntropyInput SENDER_EI{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
const S2::SPAN::EntropyInput RECEIVER_EI{
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20};
const S2::SPAN::Personalization PERSONALIZATION{0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
                                                0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5,
                                                0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf};

auto freshSpan() -> S2::SPAN::Span
{
    return S2::SPAN::Span::instantiate(SENDER_EI, RECEIVER_EI, PERSONALIZATION);
}
}  // namespace

TEST(S2Span, MixEntropyIsDeterministicAndOrderSensitive)
{
    EXPECT_EQ(S2::SPAN::mixEntropy(SENDER_EI, RECEIVER_EI), S2::SPAN::mixEntropy(SENDER_EI, RECEIVER_EI));
    EXPECT_NE(S2::SPAN::mixEntropy(SENDER_EI, RECEIVER_EI), S2::SPAN::mixEntropy(RECEIVER_EI, SENDER_EI));
}

TEST(S2Span, BothPeersGenerateTheSameSequence)
{
    auto sender   = freshSpan();
    auto receiver = freshSpan();  // same EIs + personalization on both sides
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(sender.nextNonce(), receiver.nextNonce());
    }
}

TEST(S2Span, ConsecutiveNoncesDiffer)
{
    auto span         = freshSpan();
    const auto first  = span.nextNonce();
    const auto second = span.nextNonce();
    EXPECT_NE(first, second);
}

TEST(S2Span, SerializeResumesTheSequence)
{
    auto span = freshSpan();
    static_cast<void>(span.nextNonce());  // advance once
    // A SPAN restored from the persisted inner state must continue the exact
    // same sequence — this is what lets a daemon restart avoid a Nonce Sync.
    auto resumed = S2::SPAN::Span::deserialize(span.serialize());
    EXPECT_EQ(span.nextNonce(), resumed.nextNonce());
}

TEST(S2Span, CounterRolloverWraps)
{
    S2::Crypto::Block allOnes{};
    allOnes.fill(0xFF);
    S2::SPAN::incrementCounter(allOnes);
    EXPECT_EQ(allOnes, S2::Crypto::Block{});  // all zero — full wrap

    S2::Crypto::Block carry{};
    carry.back() = 0xFF;  // ...00 FF
    S2::SPAN::incrementCounter(carry);
    S2::Crypto::Block expected{};
    expected.at(expected.size() - 2) = 0x01;  // ...01 00
    EXPECT_EQ(carry, expected);
}

TEST(S2Span, DesyncThenResync)
{
    auto sender   = freshSpan();
    auto receiver = freshSpan();
    static_cast<void>(sender.nextNonce());  // sender pulls one ahead — desync

    EXPECT_NE(sender.nextNonce(), receiver.nextNonce());

    // Nonce Sync re-instantiates both from the same fresh entropy → back in step.
    auto senderResynced   = freshSpan();
    auto receiverResynced = freshSpan();
    EXPECT_EQ(senderResynced.nextNonce(), receiverResynced.nextNonce());
}

TEST(S2Span, TablePerPeer)
{
    constexpr std::uint8_t peer  = 5;
    constexpr std::uint8_t other = 6;
    S2::SPAN::Table table;
    EXPECT_FALSE(table.has(peer));
    EXPECT_FALSE(table.nextNonce(peer).has_value());

    table.establish(peer, SENDER_EI, RECEIVER_EI, PERSONALIZATION);
    EXPECT_TRUE(table.has(peer));
    EXPECT_TRUE(table.nextNonce(peer).has_value());
    EXPECT_FALSE(table.nextNonce(other).has_value());

    table.remove(peer);
    EXPECT_FALSE(table.has(peer));
}
