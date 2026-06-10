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

TEST(MultiChannel, EncodeEndpointGet)
{
    EXPECT_EQ(MultiChannel::encodeEndpointGet(), (std::vector<std::uint8_t>{CC_MULTI_CHANNEL, 0x07}));
}

TEST(MultiChannel, DecodeEndpointReport)
{
    // properties1 0xC0 = dynamic | identical; properties2 0x03 = 3 endpoints.
    const std::array<std::uint8_t, 4> bytes{CC_MULTI_CHANNEL, 0x08, 0xC0, 0x03};
    const auto report = MultiChannel::decodeEndpointReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->endpointCount, 3);
    EXPECT_TRUE(report->dynamic);
    EXPECT_TRUE(report->identical);
}

TEST(MultiChannel, DecodeEndpointReportMasksCountAndFlags)
{
    // properties1 0x00 = neither flag; properties2 0x82 -> count 2 (bit7 masked off).
    const std::array<std::uint8_t, 4> bytes{CC_MULTI_CHANNEL, 0x08, 0x00, 0x82};
    const auto report = MultiChannel::decodeEndpointReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->endpointCount, 2);
    EXPECT_FALSE(report->dynamic);
    EXPECT_FALSE(report->identical);
}

TEST(MultiChannel, EncodeCapabilityGetMasksEndpoint)
{
    EXPECT_EQ(MultiChannel::encodeCapabilityGet(0x82),  // bit 7 masked off -> 2
              (std::vector<std::uint8_t>{CC_MULTI_CHANNEL, 0x09, 0x02}));
}

TEST(MultiChannel, DecodeCapabilityReport)
{
    // endpoint 2, generic 0x10, specific 0x01, CCs {0x25, 0x20}.
    const std::array<std::uint8_t, 7> bytes{CC_MULTI_CHANNEL, 0x0A, 0x02, 0x10, 0x01, 0x25, 0x20};
    const auto report = MultiChannel::decodeCapabilityReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->endpoint, 2);
    EXPECT_EQ(report->generic, 0x10);
    EXPECT_EQ(report->specific, 0x01);
    EXPECT_EQ(report->commandClasses, (std::vector<std::uint8_t>{0x25, 0x20}));
}

TEST(MultiChannel, DecodeDiscoveryRejectsMalformed)
{
    const std::array<std::uint8_t, 3> shortEp{CC_MULTI_CHANNEL, 0x08, 0x00};  // missing count byte
    EXPECT_FALSE(MultiChannel::decodeEndpointReport(std::span<const std::uint8_t>(shortEp)).has_value());
    const std::array<std::uint8_t, 4> shortCap{CC_MULTI_CHANNEL, 0x0A, 0x02, 0x10};  // missing specific
    EXPECT_FALSE(MultiChannel::decodeCapabilityReport(std::span<const std::uint8_t>(shortCap)).has_value());
    // A capability report with an empty CC list is still valid (5 bytes).
    const std::array<std::uint8_t, 5> noCcs{CC_MULTI_CHANNEL, 0x0A, 0x01, 0x10, 0x01};
    const auto report = MultiChannel::decodeCapabilityReport(std::span<const std::uint8_t>(noCcs));
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->commandClasses.empty());
}
