// Security S2 (CC 0x9F) MESSAGE_ENCAPSULATION codec (#182). Round-trip per the
// SDS13783 frame + AAD layout, and rejection of tampering / wrong key, nonce,
// or identity (the AAD binds all of those).

#include "Encapsulation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
auto context() -> S2::Encapsulation::Context
{
    return S2::Encapsulation::Context{
        .senderNodeId = 1, .receiverNodeId = 5, .homeId = {0xDE, 0xAD, 0xBE, 0xEF}, .sequenceNumber = 0x42};
}

const S2::Crypto::Key KEY{
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
const S2::Encapsulation::CcmNonce NONCE{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c};
const std::vector<std::uint8_t> INNER{0x25, 0x01, 0xFF};  // Binary Switch SET on

auto sampleFrame() -> std::vector<std::uint8_t>
{
    return S2::Encapsulation::encrypt(std::span<const std::uint8_t>(INNER), context(), KEY, NONCE);
}
}  // namespace

TEST(S2Encapsulation, FrameStructure)
{
    const auto frame = sampleFrame();
    ASSERT_GE(frame.size(), 4U + S2::Encapsulation::TAG_SIZE);
    EXPECT_EQ(frame[0], 0x9F);
    EXPECT_EQ(frame[1], 0x03);
    EXPECT_EQ(frame[2], 0x42);  // sequence number
    EXPECT_EQ(frame[3], 0x00);  // no extensions
    // [0x9F][0x03][seq][props] + (inner + 8-byte tag)
    EXPECT_EQ(frame.size(), 4U + INNER.size() + S2::Encapsulation::TAG_SIZE);
}

TEST(S2Encapsulation, RoundTrip)
{
    const auto frame = sampleFrame();
    const auto inner = S2::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), context(), KEY, NONCE);
    ASSERT_TRUE(inner.has_value());
    EXPECT_EQ(*inner, INNER);
}

TEST(S2Encapsulation, RoundTripWithNonEncryptedExtension)
{
    // A made-up non-encrypted extension object: [len=4][flags/type=0x01][data 2 bytes].
    const std::array<std::uint8_t, 4> extension{0x04, 0x01, 0xAB, 0xCD};
    const auto frame = S2::Encapsulation::encrypt(
        std::span<const std::uint8_t>(INNER), context(), KEY, NONCE, std::span<const std::uint8_t>(extension));
    EXPECT_EQ(frame[3], 0x01);  // Extension bit set
    // The extension travels in the clear, between props and the ciphertext.
    EXPECT_TRUE(std::equal(extension.begin(), extension.end(), frame.begin() + 4));

    const auto inner = S2::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), context(), KEY, NONCE);
    ASSERT_TRUE(inner.has_value());
    EXPECT_EQ(*inner, INNER);  // extension is authenticated (AAD) but not part of the inner command
}

TEST(S2Encapsulation, TamperedCiphertextRejected)
{
    auto frame = sampleFrame();
    frame[5] ^= 0x01;  // flip a ciphertext byte
    EXPECT_FALSE(S2::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), context(), KEY, NONCE).has_value());
}

TEST(S2Encapsulation, TamperedHeaderRejected)
{
    auto frame = sampleFrame();
    frame[2] ^= 0xFF;  // change the sequence number — it's in the AAD
    EXPECT_FALSE(S2::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), context(), KEY, NONCE).has_value());
}

TEST(S2Encapsulation, WrongKeyRejected)
{
    const auto frame      = sampleFrame();
    S2::Crypto::Key wrong = KEY;
    wrong[0] ^= 0x01;
    EXPECT_FALSE(S2::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), context(), wrong, NONCE).has_value());
}

TEST(S2Encapsulation, WrongNonceRejected)
{
    const auto frame                  = sampleFrame();
    S2::Encapsulation::CcmNonce wrong = NONCE;
    wrong[0] ^= 0x01;
    EXPECT_FALSE(S2::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), context(), KEY, wrong).has_value());
}

TEST(S2Encapsulation, WrongIdentityRejected)
{
    const auto frame     = sampleFrame();
    auto other           = context();
    other.receiverNodeId = 9;  // AAD binds the receiver id
    EXPECT_FALSE(S2::Encapsulation::decrypt(std::span<const std::uint8_t>(frame), other, KEY, NONCE).has_value());
}
