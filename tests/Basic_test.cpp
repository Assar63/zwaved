#include "Basic.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_BASIC      = 0x20;
constexpr std::uint8_t CMD_SET       = 0x01;
constexpr std::uint8_t CMD_GET       = 0x02;
constexpr std::uint8_t CMD_REPORT    = 0x03;
constexpr std::uint8_t VALUE_OFF     = 0x00;
constexpr std::uint8_t VALUE_ON      = 0xFF;
constexpr std::uint8_t VALUE_UNKNOWN = 0xFE;
}  // namespace

TEST(Basic, EncodeSetOff)
{
    const std::vector<std::uint8_t> expected{CC_BASIC, CMD_SET, VALUE_OFF};
    EXPECT_EQ(Basic::encodeSet(VALUE_OFF), expected);
}

TEST(Basic, EncodeSetOn)
{
    const std::vector<std::uint8_t> expected{CC_BASIC, CMD_SET, VALUE_ON};
    EXPECT_EQ(Basic::encodeSet(VALUE_ON), expected);
}

TEST(Basic, EncodeSetDimmerLevel)
{
    // Levels 1..99 (0x01..0x63) are the dimmer range.
    const std::vector<std::uint8_t> expected{CC_BASIC, CMD_SET, 0x32};
    EXPECT_EQ(Basic::encodeSet(0x32), expected);
}

TEST(Basic, EncodeGet)
{
    const std::vector<std::uint8_t> expected{CC_BASIC, CMD_GET};
    EXPECT_EQ(Basic::encodeGet(), expected);
}

TEST(Basic, DecodeReportV1)
{
    // v1 wire form: just currentValue. The decoder must mirror
    // currentValue into targetValue (no transition info available)
    // and flag wireFormatV2 = false so callers can distinguish.
    const std::array<std::uint8_t, 3> bytes{CC_BASIC, CMD_REPORT, 0x42};
    const auto report = Basic::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->currentValue, 0x42);
    EXPECT_EQ(report->targetValue, 0x42);
    EXPECT_EQ(report->duration, 0);
    EXPECT_FALSE(report->wireFormatV2);
}

TEST(Basic, DecodeReportV2WithTransition)
{
    // v2 wire form: currentValue, targetValue, duration. The dimmer
    // is at 0x10 and ramping up to 0xFF over 5 seconds.
    const std::array<std::uint8_t, 5> bytes{CC_BASIC, CMD_REPORT, 0x10, VALUE_ON, 0x05};
    const auto report = Basic::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->currentValue, 0x10);
    EXPECT_EQ(report->targetValue, VALUE_ON);
    EXPECT_EQ(report->duration, 0x05);
    EXPECT_TRUE(report->wireFormatV2);
}

TEST(Basic, DecodeReportUnknownValue)
{
    const std::array<std::uint8_t, 3> bytes{CC_BASIC, CMD_REPORT, VALUE_UNKNOWN};
    const auto report = Basic::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->currentValue, VALUE_UNKNOWN);
}

TEST(Basic, DecodeReportRejectsTooShort)
{
    const std::array<std::uint8_t, 2> bytes{CC_BASIC, CMD_REPORT};
    EXPECT_FALSE(Basic::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Basic, DecodeReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 3> bytes{0x25, CMD_REPORT, VALUE_ON};
    EXPECT_FALSE(Basic::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Basic, DecodeReportRejectsWrongCommand)
{
    // SET is not a Report — refuse so an unsolicited inbound SET
    // (some controllers route Basic SET as a notification) doesn't
    // get parsed as state.
    const std::array<std::uint8_t, 3> bytes{CC_BASIC, CMD_SET, VALUE_ON};
    EXPECT_FALSE(Basic::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}
