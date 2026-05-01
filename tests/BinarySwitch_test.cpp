#include "BinarySwitch.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_SWITCH_BINARY = 0x25;
constexpr std::uint8_t CMD_SET          = 0x01;
constexpr std::uint8_t CMD_GET          = 0x02;
constexpr std::uint8_t CMD_REPORT       = 0x03;
constexpr std::uint8_t VALUE_OFF        = 0x00;
constexpr std::uint8_t VALUE_ON         = 0xFF;
constexpr std::uint8_t VALUE_UNKNOWN    = 0xFE;
}  // namespace

TEST(BinarySwitch, EncodeSetOn)
{
    const std::vector<std::uint8_t> expected{CC_SWITCH_BINARY, CMD_SET, VALUE_ON};
    EXPECT_EQ(BinarySwitch::encodeSet(true), expected);
}

TEST(BinarySwitch, EncodeSetOff)
{
    const std::vector<std::uint8_t> expected{CC_SWITCH_BINARY, CMD_SET, VALUE_OFF};
    EXPECT_EQ(BinarySwitch::encodeSet(false), expected);
}

TEST(BinarySwitch, EncodeGet)
{
    const std::vector<std::uint8_t> expected{CC_SWITCH_BINARY, CMD_GET};
    EXPECT_EQ(BinarySwitch::encodeGet(), expected);
}

TEST(BinarySwitch, DecodeReportOn)
{
    const std::array<std::uint8_t, 3> bytes{CC_SWITCH_BINARY, CMD_REPORT, VALUE_ON};
    const auto report = BinarySwitch::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->state, BinarySwitch::State::On);
    EXPECT_EQ(report->rawValue, VALUE_ON);
}

TEST(BinarySwitch, DecodeReportOff)
{
    const std::array<std::uint8_t, 3> bytes{CC_SWITCH_BINARY, CMD_REPORT, VALUE_OFF};
    const auto report = BinarySwitch::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->state, BinarySwitch::State::Off);
    EXPECT_EQ(report->rawValue, VALUE_OFF);
}

TEST(BinarySwitch, DecodeReportUnknown)
{
    const std::array<std::uint8_t, 3> bytes{CC_SWITCH_BINARY, CMD_REPORT, VALUE_UNKNOWN};
    const auto report = BinarySwitch::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->state, BinarySwitch::State::Unknown);
    EXPECT_EQ(report->rawValue, VALUE_UNKNOWN);
}

TEST(BinarySwitch, DecodeReportNonZeroValueIsOn)
{
    // Per Z-Wave spec §2.2.20.3 Table 2.107, any non-zero value other
    // than 0xFE is reported as the On state.
    const std::array<std::uint8_t, 3> bytes{CC_SWITCH_BINARY, CMD_REPORT, 0x42};
    const auto report = BinarySwitch::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->state, BinarySwitch::State::On);
    EXPECT_EQ(report->rawValue, 0x42);
}

TEST(BinarySwitch, DecodeReportRejectsTooShort)
{
    const std::array<std::uint8_t, 2> bytes{CC_SWITCH_BINARY, CMD_REPORT};
    EXPECT_FALSE(BinarySwitch::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(BinarySwitch, DecodeReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 3> bytes{0x26, CMD_REPORT, VALUE_ON};
    EXPECT_FALSE(BinarySwitch::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(BinarySwitch, DecodeReportRejectsWrongCommand)
{
    // SET (0x01) is not a Report — the decoder must refuse so we
    // don't accidentally turn an unsolicited SET into a phantom
    // state update.
    const std::array<std::uint8_t, 3> bytes{CC_SWITCH_BINARY, CMD_SET, VALUE_ON};
    EXPECT_FALSE(BinarySwitch::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}
