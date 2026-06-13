// Security S2 (CC 0x9F) public-key exchange + DSK helpers (#184).

#include "PublicKey.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// First 16 bytes encode DSK groups 1..8 as the values 1..8; the rest is filler.
auto sampleKey() -> S2::Crypto::PublicKey
{
    S2::Crypto::PublicKey key{};
    for (std::uint16_t group = 0; group < 8; ++group)
    {
        key.at(group * 2)       = 0x00;
        key.at((group * 2) + 1) = static_cast<std::uint8_t>(group + 1);
    }
    for (std::size_t i = 16; i < key.size(); ++i)
    {
        key.at(i) = static_cast<std::uint8_t>(0xA0 + i);
    }
    return key;
}
}  // namespace

TEST(S2PublicKey, EncodeDecodeRoundTrip)
{
    const auto key   = sampleKey();
    const auto frame = S2::PublicKey::encode(true, key, S2::PublicKey::OBFUSCATE_NONE);
    ASSERT_EQ(frame.size(), 3U + S2::Crypto::CURVE25519_KEY_SIZE);
    EXPECT_EQ(frame[0], 0x9F);
    EXPECT_EQ(frame[1], 0x08);
    const auto report = S2::PublicKey::decode(std::span<const std::uint8_t>(frame));
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->includingNode);
    EXPECT_EQ(report->key, key);
}

TEST(S2PublicKey, EncodeObfuscatesLeadingBytes)
{
    const auto key   = sampleKey();
    const auto frame = S2::PublicKey::encode(false, key, S2::PublicKey::OBFUSCATE_DSK);
    EXPECT_EQ(frame[2], 0x00);  // Including Node flag clear
    EXPECT_EQ(frame[3], 0x00);  // first 2 key bytes zeroed
    EXPECT_EQ(frame[4], 0x00);
    EXPECT_EQ(frame[5], key[2]);  // third byte intact
}

TEST(S2PublicKey, DskStringAndPin)
{
    const auto key = sampleKey();
    EXPECT_EQ(S2::PublicKey::dskString(key), "00001-00002-00003-00004-00005-00006-00007-00008");
    EXPECT_EQ(S2::PublicKey::dskPin(key), "00001");
}

TEST(S2PublicKey, ParsePin)
{
    EXPECT_EQ(S2::PublicKey::parsePin("00042"), std::optional<std::uint16_t>{42});
    EXPECT_EQ(S2::PublicKey::parsePin("65535"), std::optional<std::uint16_t>{65535});
    EXPECT_FALSE(S2::PublicKey::parsePin("70000").has_value());  // > 65535
    EXPECT_FALSE(S2::PublicKey::parsePin("1234").has_value());   // too few digits
    EXPECT_FALSE(S2::PublicKey::parsePin("12a45").has_value());  // non-digit
}

TEST(S2PublicKey, ApplyPinRestoresKey)
{
    const auto original = sampleKey();
    // Obfuscate as the joining node would (zero the first 2 bytes).
    auto obfuscated  = original;
    obfuscated.at(0) = 0;
    obfuscated.at(1) = 0;

    const auto pin = S2::PublicKey::parsePin(S2::PublicKey::dskPin(original));
    ASSERT_TRUE(pin.has_value());
    EXPECT_EQ(S2::PublicKey::applyPin(obfuscated, *pin), original);
}

TEST(S2PublicKey, ReconstructedKeyAgreesViaEcdh)
{
    const auto controller = S2::Crypto::generateKeyPair();
    const auto node       = S2::Crypto::generateKeyPair();

    // Node sends its public key with the first 2 bytes obfuscated.
    auto obfuscated  = node.publicKey;
    obfuscated.at(0) = 0;
    obfuscated.at(1) = 0;
    const auto pin   = S2::PublicKey::parsePin(S2::PublicKey::dskPin(node.publicKey));
    ASSERT_TRUE(pin.has_value());
    const auto reconstructed = S2::PublicKey::applyPin(obfuscated, *pin);

    // ECDH against the reconstructed key matches the node's own derivation.
    const auto controllerSide = S2::Crypto::ecdh(controller.privateKey, reconstructed);
    const auto nodeSide       = S2::Crypto::ecdh(node.privateKey, controller.publicKey);
    ASSERT_TRUE(controllerSide.has_value());
    ASSERT_TRUE(nodeSide.has_value());
    EXPECT_EQ(*controllerSide, *nodeSide);
}
