#include "ThermostatMode.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
auto report(std::uint8_t modeByte) -> std::vector<std::uint8_t>
{
    return {ThermostatMode::COMMAND_CLASS, ThermostatMode::THERMOSTAT_MODE_REPORT, modeByte};
}
}  // namespace

TEST(ThermostatMode, EncodeSet)
{
    const std::vector<std::uint8_t> expected{
        ThermostatMode::COMMAND_CLASS, ThermostatMode::THERMOSTAT_MODE_SET, ThermostatMode::MODE_HEAT};
    EXPECT_EQ(ThermostatMode::encodeSet(ThermostatMode::MODE_HEAT), expected);
}

TEST(ThermostatMode, EncodeGet)
{
    const std::vector<std::uint8_t> expected{ThermostatMode::COMMAND_CLASS, ThermostatMode::THERMOSTAT_MODE_GET};
    EXPECT_EQ(ThermostatMode::encodeGet(), expected);
}

TEST(ThermostatMode, DecodeBasicModes)
{
    const auto off = ThermostatMode::decodeReport(report(ThermostatMode::MODE_OFF));
    ASSERT_TRUE(off.has_value());
    EXPECT_EQ(off->mode, ThermostatMode::MODE_OFF);

    const auto cool = ThermostatMode::decodeReport(report(ThermostatMode::MODE_COOL));
    ASSERT_TRUE(cool.has_value());
    EXPECT_EQ(cool->mode, ThermostatMode::MODE_COOL);
}

TEST(ThermostatMode, DecodeMasksManufacturerDataLength)
{
    // High 3 bits carry a v3 manufacturer-data-field count; low 5 bits are
    // the mode. 0x68 = (data length 3 << 5) | 0x08 (dry) → decodes as 8.
    const auto decoded = ThermostatMode::decodeReport(report(0x68));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->mode, 0x08);
}

TEST(ThermostatMode, RejectsMalformed)
{
    // Too short (no mode byte).
    const std::vector<std::uint8_t> tooShort{ThermostatMode::COMMAND_CLASS, ThermostatMode::THERMOSTAT_MODE_REPORT};
    EXPECT_FALSE(ThermostatMode::decodeReport(tooShort).has_value());

    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{0x25, ThermostatMode::THERMOSTAT_MODE_REPORT, 0x01};
    EXPECT_FALSE(ThermostatMode::decodeReport(wrongCc).has_value());

    // Wrong command byte (Get, not Report).
    const std::vector<std::uint8_t> wrongCmd{ThermostatMode::COMMAND_CLASS, ThermostatMode::THERMOSTAT_MODE_GET, 0x01};
    EXPECT_FALSE(ThermostatMode::decodeReport(wrongCmd).has_value());
}
