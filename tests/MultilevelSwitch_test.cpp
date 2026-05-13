#include "MultilevelSwitch.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_SWITCH_MULTILEVEL = 0x26;
constexpr std::uint8_t CMD_SET              = 0x01;
constexpr std::uint8_t CMD_GET              = 0x02;
constexpr std::uint8_t CMD_REPORT           = 0x03;
constexpr std::uint8_t VALUE_OFF            = 0x00;
constexpr std::uint8_t VALUE_MID            = 0x32;  // 50%
constexpr std::uint8_t VALUE_MAX            = 0x63;  // 99 — spec max level
constexpr std::uint8_t VALUE_RESTORE        = 0xFF;
constexpr std::uint8_t VALUE_UNKNOWN        = 0xFE;
constexpr std::uint8_t DURATION_INSTANT     = 0x00;
constexpr std::uint8_t DURATION_DEFAULT     = 0xFF;
}  // namespace

TEST(MultilevelSwitch, EncodeSetOffInstant)
{
    const std::vector<std::uint8_t> expected{CC_SWITCH_MULTILEVEL, CMD_SET, VALUE_OFF, DURATION_INSTANT};
    EXPECT_EQ(MultilevelSwitch::encodeSet(VALUE_OFF, DURATION_INSTANT), expected);
}

TEST(MultilevelSwitch, EncodeSetMidLevelWithDuration)
{
    // 50 % over 5 seconds — typical dimmer transition.
    const std::vector<std::uint8_t> expected{CC_SWITCH_MULTILEVEL, CMD_SET, VALUE_MID, 0x05};
    EXPECT_EQ(MultilevelSwitch::encodeSet(VALUE_MID, 0x05), expected);
}

TEST(MultilevelSwitch, EncodeSetRestoreLastWithDefaultDuration)
{
    const std::vector<std::uint8_t> expected{CC_SWITCH_MULTILEVEL, CMD_SET, VALUE_RESTORE, DURATION_DEFAULT};
    EXPECT_EQ(MultilevelSwitch::encodeSet(VALUE_RESTORE, DURATION_DEFAULT), expected);
}

TEST(MultilevelSwitch, EncodeGet)
{
    const std::vector<std::uint8_t> expected{CC_SWITCH_MULTILEVEL, CMD_GET};
    EXPECT_EQ(MultilevelSwitch::encodeGet(), expected);
}

TEST(MultilevelSwitch, DecodeReportV1)
{
    // v1 wire form: just currentValue. The decoder mirrors current
    // into target (no transition info available) and flags
    // wireFormatV2 = false so callers can distinguish.
    const std::array<std::uint8_t, 3> bytes{CC_SWITCH_MULTILEVEL, CMD_REPORT, VALUE_MID};
    const auto report = MultilevelSwitch::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->currentValue, VALUE_MID);
    EXPECT_EQ(report->targetValue, VALUE_MID);
    EXPECT_EQ(report->duration, 0);
    EXPECT_FALSE(report->wireFormatV2);
}

TEST(MultilevelSwitch, DecodeReportV2WithTransition)
{
    // v2 wire form: currentValue, targetValue, duration. The dimmer
    // is at 0x10 and ramping up to 0x63 over 5 seconds.
    const std::array<std::uint8_t, 5> bytes{CC_SWITCH_MULTILEVEL, CMD_REPORT, 0x10, VALUE_MAX, 0x05};
    const auto report = MultilevelSwitch::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->currentValue, 0x10);
    EXPECT_EQ(report->targetValue, VALUE_MAX);
    EXPECT_EQ(report->duration, 0x05);
    EXPECT_TRUE(report->wireFormatV2);
}

TEST(MultilevelSwitch, DecodeReportUnknownValue)
{
    const std::array<std::uint8_t, 3> bytes{CC_SWITCH_MULTILEVEL, CMD_REPORT, VALUE_UNKNOWN};
    const auto report = MultilevelSwitch::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->currentValue, VALUE_UNKNOWN);
}

TEST(MultilevelSwitch, DecodeReportRejectsTooShort)
{
    const std::array<std::uint8_t, 2> bytes{CC_SWITCH_MULTILEVEL, CMD_REPORT};
    EXPECT_FALSE(MultilevelSwitch::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(MultilevelSwitch, DecodeReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 3> bytes{0x25, CMD_REPORT, VALUE_MID};
    EXPECT_FALSE(MultilevelSwitch::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(MultilevelSwitch, DecodeReportRejectsWrongCommand)
{
    // SET is not a Report — refuse so an unsolicited inbound SET
    // doesn't accidentally turn into a phantom state update.
    const std::array<std::uint8_t, 3> bytes{CC_SWITCH_MULTILEVEL, CMD_SET, VALUE_MID};
    EXPECT_FALSE(MultilevelSwitch::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}
