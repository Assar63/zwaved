#include "MultiChannel.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_MULTI_CHANNEL = 0x60;
constexpr std::uint8_t CMD_ENCAP        = 0x0D;
}  // namespace

TEST(MultiChannel, DecodeEncapExtractsEndpointsAndInner)
{
    // src endpoint 1, dst endpoint 3, inner = Binary Switch Set ON (0x25 0x01 0xFF).
    const std::array<std::uint8_t, 7> bytes{CC_MULTI_CHANNEL, CMD_ENCAP, 0x01, 0x03, 0x25, 0x01, 0xFF};
    const auto decoded = MultiChannel::decodeEncap(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sourceEndpoint, 1);
    EXPECT_EQ(decoded->destinationEndpoint, 3);
    EXPECT_FALSE(decoded->bitAddress);
    EXPECT_EQ(decoded->innerCommand, (std::vector<std::uint8_t>{0x25, 0x01, 0xFF}));
}

TEST(MultiChannel, DecodeEncapMasksEndpointBitsAndBitAddress)
{
    // dst byte 0x85 = bit-address flag (0x80) | endpoint 5; src byte 0x82 -> endpoint 2.
    const std::array<std::uint8_t, 6> bytes{CC_MULTI_CHANNEL, CMD_ENCAP, 0x82, 0x85, 0x20, 0x00};
    const auto decoded = MultiChannel::decodeEncap(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sourceEndpoint, 2);
    EXPECT_EQ(decoded->destinationEndpoint, 5);
    EXPECT_TRUE(decoded->bitAddress);
    EXPECT_EQ(decoded->innerCommand, (std::vector<std::uint8_t>{0x20, 0x00}));
}

TEST(MultiChannel, DecodeEncapRejectsMalformed)
{
    // No inner command (header only).
    const std::array<std::uint8_t, 4> noInner{CC_MULTI_CHANNEL, CMD_ENCAP, 0x01, 0x02};
    EXPECT_FALSE(MultiChannel::decodeEncap(std::span<const std::uint8_t>(noInner)).has_value());
    // Wrong command class.
    const std::array<std::uint8_t, 5> wrongCc{0x25, CMD_ENCAP, 0x01, 0x02, 0x25};
    EXPECT_FALSE(MultiChannel::decodeEncap(std::span<const std::uint8_t>(wrongCc)).has_value());
    // Wrong command byte.
    const std::array<std::uint8_t, 5> wrongCmd{CC_MULTI_CHANNEL, 0x0E, 0x01, 0x02, 0x25};
    EXPECT_FALSE(MultiChannel::decodeEncap(std::span<const std::uint8_t>(wrongCmd)).has_value());
}

TEST(MultiChannel, EncodeEncapWrapsInner)
{
    const std::array<std::uint8_t, 3> inner{0x25, 0x01, 0x00};
    const auto frame = MultiChannel::encodeEncap(2, 4, std::span<const std::uint8_t>(inner));
    EXPECT_EQ(frame, (std::vector<std::uint8_t>{CC_MULTI_CHANNEL, CMD_ENCAP, 0x02, 0x04, 0x25, 0x01, 0x00}));
}

TEST(MultiChannel, EncodeDecodeRoundTrip)
{
    const std::array<std::uint8_t, 4> inner{0x26, 0x01, 0x63, 0x00};
    const auto frame   = MultiChannel::encodeEncap(1, 7, std::span<const std::uint8_t>(inner));
    const auto decoded = MultiChannel::decodeEncap(std::span<const std::uint8_t>(frame));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sourceEndpoint, 1);
    EXPECT_EQ(decoded->destinationEndpoint, 7);
    EXPECT_EQ(decoded->innerCommand, (std::vector<std::uint8_t>{0x26, 0x01, 0x63, 0x00}));
}
