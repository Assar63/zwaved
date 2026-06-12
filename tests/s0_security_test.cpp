#include "Security.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_SECURITY = 0x98;
constexpr std::uint8_t CMD_GET     = 0x40;
constexpr std::uint8_t CMD_REPORT  = 0x80;

const S0::Nonce SAMPLE{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
}  // namespace

TEST(S0Security, EncodeNonceGet)
{
    EXPECT_EQ(Security::encodeNonceGet(), (std::vector<std::uint8_t>{CC_SECURITY, CMD_GET}));
}

TEST(S0Security, EncodeNonceReportLayout)
{
    const auto frame = Security::encodeNonceReport(SAMPLE);
    ASSERT_EQ(frame.size(), 2U + S0::NONCE_SIZE);
    EXPECT_EQ(frame[0], CC_SECURITY);
    EXPECT_EQ(frame[1], CMD_REPORT);
    EXPECT_TRUE(std::equal(SAMPLE.begin(), SAMPLE.end(), frame.begin() + 2));
}

TEST(S0Security, NonceReportRoundTrip)
{
    const auto frame   = Security::encodeNonceReport(SAMPLE);
    const auto decoded = Security::decodeNonceReport(std::span<const std::uint8_t>(frame));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, SAMPLE);
}

TEST(S0Security, DecodeRejectsMalformed)
{
    // Wrong command.
    const std::vector<std::uint8_t> wrongCmd{CC_SECURITY, CMD_GET, 1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_FALSE(Security::decodeNonceReport(std::span<const std::uint8_t>(wrongCmd)).has_value());
    // Wrong class.
    const std::vector<std::uint8_t> wrongCc{0x25, CMD_REPORT, 1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_FALSE(Security::decodeNonceReport(std::span<const std::uint8_t>(wrongCc)).has_value());
    // Too short.
    const std::vector<std::uint8_t> shortFrame{CC_SECURITY, CMD_REPORT, 1, 2, 3};
    EXPECT_FALSE(Security::decodeNonceReport(std::span<const std::uint8_t>(shortFrame)).has_value());
}

TEST(S0Security, CommandByte)
{
    const std::vector<std::uint8_t> get{CC_SECURITY, CMD_GET};
    const auto cmd = Security::commandByte(std::span<const std::uint8_t>(get));
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(*cmd, CMD_GET);

    const std::vector<std::uint8_t> notSecurity{0x25, 0x01, 0xFF};
    EXPECT_FALSE(Security::commandByte(std::span<const std::uint8_t>(notSecurity)).has_value());
}
