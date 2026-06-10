#include "Indicator.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_INDICATOR = 0x87;
constexpr std::uint8_t CMD_SET      = 0x01;
constexpr std::uint8_t CMD_GET      = 0x02;
constexpr std::uint8_t CMD_REPORT   = 0x03;
constexpr std::uint8_t VALUE_ON     = 0xFF;
constexpr std::uint8_t VALUE_OFF    = 0x00;
}  // namespace

TEST(Indicator, EncodeSetOnAndOff)
{
    EXPECT_EQ(Indicator::encodeSet(VALUE_ON), (std::vector<std::uint8_t>{CC_INDICATOR, CMD_SET, VALUE_ON}));
    EXPECT_EQ(Indicator::encodeSet(VALUE_OFF), (std::vector<std::uint8_t>{CC_INDICATOR, CMD_SET, VALUE_OFF}));
    EXPECT_EQ(Indicator::encodeSet(0x32), (std::vector<std::uint8_t>{CC_INDICATOR, CMD_SET, 0x32}));  // a level
}

TEST(Indicator, EncodeGet)
{
    EXPECT_EQ(Indicator::encodeGet(), (std::vector<std::uint8_t>{CC_INDICATOR, CMD_GET}));
}

TEST(Indicator, DecodeReportV1)
{
    const std::array<std::uint8_t, 3> bytes{CC_INDICATOR, CMD_REPORT, VALUE_ON};
    const auto report = Indicator::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->value, VALUE_ON);
}

TEST(Indicator, DecodeReportRejectsMalformed)
{
    // Too short (missing value byte).
    const std::array<std::uint8_t, 2> tooShort{CC_INDICATOR, CMD_REPORT};
    EXPECT_FALSE(Indicator::decodeReport(std::span<const std::uint8_t>(tooShort)).has_value());
    // Wrong command class.
    const std::array<std::uint8_t, 3> wrongCc{0x25, CMD_REPORT, VALUE_ON};
    EXPECT_FALSE(Indicator::decodeReport(std::span<const std::uint8_t>(wrongCc)).has_value());
    // Wrong command (SET, not REPORT).
    const std::array<std::uint8_t, 3> wrongCmd{CC_INDICATOR, CMD_SET, VALUE_ON};
    EXPECT_FALSE(Indicator::decodeReport(std::span<const std::uint8_t>(wrongCmd)).has_value());
}
