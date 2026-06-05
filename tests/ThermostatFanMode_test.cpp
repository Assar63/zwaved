#include "ThermostatFanMode.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
auto report(std::uint8_t propertiesByte) -> std::vector<std::uint8_t>
{
    return {ThermostatFanMode::COMMAND_CLASS, ThermostatFanMode::THERMOSTAT_FAN_MODE_REPORT, propertiesByte};
}
}  // namespace

TEST(ThermostatFanMode, EncodeGet)
{
    const std::vector<std::uint8_t> expected{ThermostatFanMode::COMMAND_CLASS,
                                             ThermostatFanMode::THERMOSTAT_FAN_MODE_GET};
    EXPECT_EQ(ThermostatFanMode::encodeGet(), expected);
}

TEST(ThermostatFanMode, EncodeSetModeOnly)
{
    // mode=high (3), off=false → properties 0x03.
    const std::vector<std::uint8_t> expected{
        ThermostatFanMode::COMMAND_CLASS, ThermostatFanMode::THERMOSTAT_FAN_MODE_SET, 0x03};
    EXPECT_EQ(ThermostatFanMode::encodeSet(ThermostatFanMode::MODE_HIGH, false), expected);
}

TEST(ThermostatFanMode, EncodeSetWithOffFlag)
{
    // mode=low (1), off=true → properties (0x80 | 0x01) = 0x81.
    const std::vector<std::uint8_t> expected{
        ThermostatFanMode::COMMAND_CLASS, ThermostatFanMode::THERMOSTAT_FAN_MODE_SET, 0x81};
    EXPECT_EQ(ThermostatFanMode::encodeSet(ThermostatFanMode::MODE_LOW, true), expected);
}

TEST(ThermostatFanMode, DecodeModeOnly)
{
    const auto decoded = ThermostatFanMode::decodeReport(report(ThermostatFanMode::MODE_AUTO_HIGH));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->mode, ThermostatFanMode::MODE_AUTO_HIGH);
    EXPECT_FALSE(decoded->off);
}

TEST(ThermostatFanMode, DecodeSplitsOffFlagFromMode)
{
    // 0x83 = off flag (0x80) | mode 3 (high).
    const auto decoded = ThermostatFanMode::decodeReport(report(0x83));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->mode, ThermostatFanMode::MODE_HIGH);
    EXPECT_TRUE(decoded->off);
}

TEST(ThermostatFanMode, RejectsMalformed)
{
    // Too short (no properties byte).
    const std::vector<std::uint8_t> tooShort{ThermostatFanMode::COMMAND_CLASS,
                                             ThermostatFanMode::THERMOSTAT_FAN_MODE_REPORT};
    EXPECT_FALSE(ThermostatFanMode::decodeReport(tooShort).has_value());

    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{0x40, ThermostatFanMode::THERMOSTAT_FAN_MODE_REPORT, 0x01};
    EXPECT_FALSE(ThermostatFanMode::decodeReport(wrongCc).has_value());

    // Wrong command byte (Get, not Report).
    const std::vector<std::uint8_t> wrongCmd{
        ThermostatFanMode::COMMAND_CLASS, ThermostatFanMode::THERMOSTAT_FAN_MODE_GET, 0x01};
    EXPECT_FALSE(ThermostatFanMode::decodeReport(wrongCmd).has_value());
}
