#include "Encapsulation.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CONTROLLER = 1;
constexpr std::uint8_t NODE       = 5;

// Arbitrary but fixed fixtures.
const S0::Crypto::Key KEY{
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
const S0::Nonce SENDER_NONCE{0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7};
const S0::Nonce RECEIVER_NONCE{0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7};
const std::vector<std::uint8_t> INNER{0x25, 0x01, 0xFF};  // Binary Switch SET on

auto sampleFrame() -> std::vector<std::uint8_t>
{
    return S0::Encapsulation::encrypt(
        std::span<const std::uint8_t>(INNER), CONTROLLER, NODE, SENDER_NONCE, RECEIVER_NONCE, KEY);
}
}  // namespace

TEST(S0Encapsulation, FrameStructure)
{
    const auto frame = sampleFrame();
    // [0x98][0x81][senderNonce·8][ciphertext = 1 seq + 3 inner][nonceId·1][MAC·8]
    ASSERT_EQ(frame.size(), 2U + 8U + (1U + INNER.size()) + 1U + 8U);
    EXPECT_EQ(frame[0], 0x98);
    EXPECT_EQ(frame[1], 0x81);
    EXPECT_TRUE(std::equal(SENDER_NONCE.begin(), SENDER_NONCE.end(), frame.begin() + 2));
    EXPECT_EQ(frame[frame.size() - 9], RECEIVER_NONCE[0]);  // receiver-nonce identifier
}

TEST(S0Encapsulation, RoundTrip)
{
    const auto frame = sampleFrame();
    // On receive, the nonce we issued is the one the sender used as receiver nonce.
    const auto inner =
        S0::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), CONTROLLER, NODE, RECEIVER_NONCE, KEY);
    ASSERT_TRUE(inner.has_value());
    EXPECT_EQ(*inner, INNER);
}

TEST(S0Encapsulation, TamperedMacRejected)
{
    auto frame = sampleFrame();
    frame.back() ^= 0xFF;  // corrupt the MAC
    EXPECT_FALSE(S0::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), CONTROLLER, NODE, RECEIVER_NONCE, KEY)
                     .has_value());
}

TEST(S0Encapsulation, TamperedCiphertextRejected)
{
    auto frame = sampleFrame();
    frame[10] ^= 0x01;  // first ciphertext byte — MAC no longer matches
    EXPECT_FALSE(S0::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), CONTROLLER, NODE, RECEIVER_NONCE, KEY)
                     .has_value());
}

TEST(S0Encapsulation, WrongReceiverNonceRejected)
{
    const auto frame = sampleFrame();
    const S0::Nonce wrong{0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7};
    EXPECT_FALSE(
        S0::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), CONTROLLER, NODE, wrong, KEY).has_value());
}

TEST(S0Encapsulation, NodeIdsBoundIntoMac)
{
    const auto frame = sampleFrame();
    // Decrypt claiming a different sender node id — the MAC covers it, so it fails.
    EXPECT_FALSE(
        S0::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), CONTROLLER + 1, NODE, RECEIVER_NONCE, KEY)
            .has_value());
}

TEST(S0Encapsulation, MalformedRejected)
{
    const auto frame = sampleFrame();
    // Too short.
    EXPECT_FALSE(S0::Encapsulation::decrypt(
                     std::span<const std::uint8_t>(frame).first(10), CONTROLLER, NODE, RECEIVER_NONCE, KEY)
                     .has_value());
    // Wrong command class.
    auto wrongCc = frame;
    wrongCc[0]   = 0x25;
    EXPECT_FALSE(
        S0::Encapsulation::decrypt(std::span<const std::uint8_t>(wrongCc), CONTROLLER, NODE, RECEIVER_NONCE, KEY)
            .has_value());
}
