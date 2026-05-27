#include "Battery.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_BATTERY        = 0x80;
constexpr std::uint8_t CMD_GET           = 0x02;
constexpr std::uint8_t CMD_REPORT        = 0x03;
constexpr std::uint8_t LEVEL_FULL        = 0x64;  // 100 %
constexpr std::uint8_t LEVEL_HALF        = 0x32;  // 50 %
constexpr std::uint8_t LEVEL_LOW_WARNING = 0xFF;
}  // namespace

TEST(Battery, EncodeGet)
{
    const std::vector<std::uint8_t> expected{CC_BATTERY, CMD_GET};
    EXPECT_EQ(Battery::encodeGet(), expected);
}

TEST(Battery, DecodeReportFull)
{
    const std::array<std::uint8_t, 3> bytes{CC_BATTERY, CMD_REPORT, LEVEL_FULL};
    const auto report = Battery::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->level, LEVEL_FULL);
    EXPECT_FALSE(report->lowBattery);
}

TEST(Battery, DecodeReportMidLevel)
{
    const std::array<std::uint8_t, 3> bytes{CC_BATTERY, CMD_REPORT, LEVEL_HALF};
    const auto report = Battery::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->level, LEVEL_HALF);
    EXPECT_FALSE(report->lowBattery);
}

TEST(Battery, DecodeReportLowBatteryWarning)
{
    // 0xFF is the spec's "low-battery, no precise gauge" sentinel.
    // decodeReport surfaces it as level=0xFF AND lowBattery=true so
    // callers can dispatch on either.
    const std::array<std::uint8_t, 3> bytes{CC_BATTERY, CMD_REPORT, LEVEL_LOW_WARNING};
    const auto report = Battery::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->level, LEVEL_LOW_WARNING);
    EXPECT_TRUE(report->lowBattery);
}

TEST(Battery, DecodeReportIgnoresV2TrailingBytes)
{
    // v2 Reports append charging / health bitfields after the level
    // byte. The decoder treats them as opaque trailing bytes and
    // returns the same Report as the v1 wire form would have.
    const std::array<std::uint8_t, 5> bytes{CC_BATTERY, CMD_REPORT, LEVEL_HALF, 0x04, 0x00};
    const auto report = Battery::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->level, LEVEL_HALF);
    EXPECT_FALSE(report->lowBattery);
}

TEST(Battery, DecodeReportRejectsTooShort)
{
    const std::array<std::uint8_t, 2> bytes{CC_BATTERY, CMD_REPORT};
    EXPECT_FALSE(Battery::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Battery, DecodeReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 3> bytes{0x25, CMD_REPORT, LEVEL_FULL};
    EXPECT_FALSE(Battery::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Battery, DecodeReportRejectsWrongCommand)
{
    // GET is not a Report — refuse so an inbound GET (which would be
    // a spec violation from a node anyway) doesn't accidentally turn
    // into a phantom state update.
    const std::array<std::uint8_t, 3> bytes{CC_BATTERY, CMD_GET, LEVEL_FULL};
    EXPECT_FALSE(Battery::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}
