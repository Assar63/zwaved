#include "Crc16Encap.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_CRC16  = 0x56;
constexpr std::uint8_t CMD_ENCAP = 0x01;
}  // namespace

TEST(Crc16Encap, ChecksumKnownVector)
{
    // CRC-16/AUG-CCITT check value for the ASCII string "123456789" is 0xE5CC.
    const std::array<std::uint8_t, 9> data{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(Crc16Encap::checksum(std::span<const std::uint8_t>(data)), 0xE5CC);
}

TEST(Crc16Encap, EncodeWrapsInnerWithCrc)
{
    // inner = Binary Switch SET on (0x25 0x01 0xFF).
    const std::array<std::uint8_t, 3> inner{0x25, 0x01, 0xFF};
    const auto frame = Crc16Encap::encode(std::span<const std::uint8_t>(inner));
    ASSERT_EQ(frame.size(), 7U);  // CC + cmd + 3 inner + 2 CRC
    EXPECT_EQ(frame[0], CC_CRC16);
    EXPECT_EQ(frame[1], CMD_ENCAP);
    // The trailer is the CRC over the first 5 bytes, big-endian.
    const std::array<std::uint8_t, 5> body{CC_CRC16, CMD_ENCAP, 0x25, 0x01, 0xFF};
    const auto crc = Crc16Encap::checksum(std::span<const std::uint8_t>(body));
    EXPECT_EQ(frame[5], static_cast<std::uint8_t>(crc >> 8));
    EXPECT_EQ(frame[6], static_cast<std::uint8_t>(crc & 0xFF));
}

TEST(Crc16Encap, EncodeVerifyRoundTrip)
{
    const std::array<std::uint8_t, 4> inner{0x31, 0x05, 0x01, 0x2A};
    const auto frame     = Crc16Encap::encode(std::span<const std::uint8_t>(inner));
    const auto unwrapped = Crc16Encap::verifyAndUnwrap(std::span<const std::uint8_t>(frame));
    ASSERT_TRUE(unwrapped.has_value());
    EXPECT_EQ(*unwrapped, (std::vector<std::uint8_t>{0x31, 0x05, 0x01, 0x2A}));
}

TEST(Crc16Encap, VerifyRejectsBadCrc)
{
    const std::array<std::uint8_t, 3> inner{0x25, 0x01, 0xFF};
    auto frame = Crc16Encap::encode(std::span<const std::uint8_t>(inner));
    frame.back() ^= 0xFF;  // corrupt the CRC
    EXPECT_FALSE(Crc16Encap::verifyAndUnwrap(std::span<const std::uint8_t>(frame)).has_value());
}

TEST(Crc16Encap, VerifyRejectsCorruptedInner)
{
    const std::array<std::uint8_t, 3> inner{0x25, 0x01, 0xFF};
    auto frame = Crc16Encap::encode(std::span<const std::uint8_t>(inner));
    frame[2] ^= 0x01;  // flip an inner bit — CRC no longer matches
    EXPECT_FALSE(Crc16Encap::verifyAndUnwrap(std::span<const std::uint8_t>(frame)).has_value());
}

TEST(Crc16Encap, VerifyRejectsMalformed)
{
    // Wrong command class.
    const std::array<std::uint8_t, 5> wrongCc{0x25, CMD_ENCAP, 0xFF, 0x00, 0x00};
    EXPECT_FALSE(Crc16Encap::verifyAndUnwrap(std::span<const std::uint8_t>(wrongCc)).has_value());
    // Too short (no room for inner + CRC).
    const std::array<std::uint8_t, 3> tooShort{CC_CRC16, CMD_ENCAP, 0x00};
    EXPECT_FALSE(Crc16Encap::verifyAndUnwrap(std::span<const std::uint8_t>(tooShort)).has_value());
}
