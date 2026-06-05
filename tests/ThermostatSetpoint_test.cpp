#include "ThermostatSetpoint.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// Build a Setpoint Report: CC + cmd + setpointType + flag + value bytes.
auto report(std::uint8_t setpointType,
            std::uint8_t flags,
            std::initializer_list<std::uint8_t> valueBytes) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{
        ThermostatSetpoint::COMMAND_CLASS, ThermostatSetpoint::THERMOSTAT_SETPOINT_REPORT, setpointType, flags};
    out.insert(out.end(), valueBytes);
    return out;
}
}  // namespace

TEST(ThermostatSetpoint, EncodeGet)
{
    const std::vector<std::uint8_t> expected{ThermostatSetpoint::COMMAND_CLASS,
                                             ThermostatSetpoint::THERMOSTAT_SETPOINT_GET,
                                             ThermostatSetpoint::TYPE_HEATING};
    EXPECT_EQ(ThermostatSetpoint::encodeGet(ThermostatSetpoint::TYPE_HEATING), expected);
}

TEST(ThermostatSetpoint, EncodeSetHeating21C)
{
    // heating, precision=1, scale=0 (°C), value 215 (>127 → 2 bytes).
    // flags = (1<<5)|(0<<3)|2 = 0x22; value 0x00D7.
    const std::vector<std::uint8_t> expected{ThermostatSetpoint::COMMAND_CLASS,
                                             ThermostatSetpoint::THERMOSTAT_SETPOINT_SET,
                                             ThermostatSetpoint::TYPE_HEATING,
                                             0x22,
                                             0x00,
                                             0xD7};
    EXPECT_EQ(ThermostatSetpoint::encodeSet(ThermostatSetpoint::TYPE_HEATING, 1, 0, 215), expected);
}

TEST(ThermostatSetpoint, EncodeSetMasksSetpointType)
{
    // High bits of setpointType are reserved — must be masked to low 4 bits.
    const auto frame = ThermostatSetpoint::encodeSet(0xF2, 0, 0, 20);
    ASSERT_GE(frame.size(), 3U);
    EXPECT_EQ(frame[2], 0x02);
}

TEST(ThermostatSetpoint, DecodeHeatingCelsius)
{
    // heating, precision=1, scale=0, size=2, value 215 → 21.5 °C.
    const auto decoded = ThermostatSetpoint::decodeReport(report(ThermostatSetpoint::TYPE_HEATING, 0x22, {0x00, 0xD7}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->setpointType, ThermostatSetpoint::TYPE_HEATING);
    EXPECT_EQ(decoded->scale, 0);
    EXPECT_EQ(decoded->precision, 1);
    EXPECT_EQ(decoded->value, 215);
}

TEST(ThermostatSetpoint, DecodeCoolingFahrenheit)
{
    // cooling, scale=1 (°F), precision=1, size=2, value 706 → 70.6 °F.
    const auto decoded = ThermostatSetpoint::decodeReport(report(ThermostatSetpoint::TYPE_COOLING, 0x2A, {0x02, 0xC2}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->setpointType, ThermostatSetpoint::TYPE_COOLING);
    EXPECT_EQ(decoded->scale, 1);
    EXPECT_EQ(decoded->value, 706);
}

TEST(ThermostatSetpoint, DecodeNegativeSignExtends)
{
    // precision=1, size=2, value -15 (0xFFF1) → -1.5.
    const auto decoded = ThermostatSetpoint::decodeReport(report(ThermostatSetpoint::TYPE_HEATING, 0x22, {0xFF, 0xF1}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, -15);
}

TEST(ThermostatSetpoint, RejectsInvalidSizeAndMalformed)
{
    // size field 3 is invalid.
    EXPECT_FALSE(ThermostatSetpoint::decodeReport(report(ThermostatSetpoint::TYPE_HEATING, 0x03, {0x00, 0x01, 0x02}))
                     .has_value());
    // truncated: flag says size 4 but only 2 value bytes.
    EXPECT_FALSE(
        ThermostatSetpoint::decodeReport(report(ThermostatSetpoint::TYPE_HEATING, 0x04, {0x00, 0x01})).has_value());
    // wrong command class.
    const std::vector<std::uint8_t> wrongCc{
        0x31, ThermostatSetpoint::THERMOSTAT_SETPOINT_REPORT, 0x01, 0x22, 0x00, 0xD7};
    EXPECT_FALSE(ThermostatSetpoint::decodeReport(wrongCc).has_value());
    // wrong command byte (Get, not Report).
    const std::vector<std::uint8_t> wrongCmd{
        ThermostatSetpoint::COMMAND_CLASS, ThermostatSetpoint::THERMOSTAT_SETPOINT_GET, 0x01, 0x22, 0x00, 0xD7};
    EXPECT_FALSE(ThermostatSetpoint::decodeReport(wrongCmd).has_value());
}

TEST(ThermostatSetpoint, SetReportRoundTrip)
{
    // A Set frame's payload (from byte 1 on) is wire-identical to a Report's,
    // so feeding an encoded Set through decodeReport (with the cmd byte
    // swapped to REPORT) recovers the fields.
    auto frame         = ThermostatSetpoint::encodeSet(ThermostatSetpoint::TYPE_HEATING, 2, 1, 1850);
    frame[1]           = ThermostatSetpoint::THERMOSTAT_SETPOINT_REPORT;
    const auto decoded = ThermostatSetpoint::decodeReport(frame);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->setpointType, ThermostatSetpoint::TYPE_HEATING);
    EXPECT_EQ(decoded->precision, 2);
    EXPECT_EQ(decoded->scale, 1);
    EXPECT_EQ(decoded->value, 1850);
}
