#include "SensorMultilevel.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// Build a Sensor Multilevel Report payload (CC + cmd + sensorType +
// flag + value bytes). `flag` packs precision<<5 | scale<<3 | size.
auto report(std::uint8_t sensorType,
            std::uint8_t flag,
            std::initializer_list<std::uint8_t> valueBytes) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{
        SensorMultilevel::COMMAND_CLASS, SensorMultilevel::SENSOR_MULTILEVEL_REPORT, sensorType, flag};
    out.insert(out.end(), valueBytes);
    return out;
}
}  // namespace

TEST(SensorMultilevel, AirTemperatureCelsius)
{
    // type=0x01 (air temp), scale=0 (°C), precision=1, size=2, value=215 → 21.5 °C
    const auto decoded = SensorMultilevel::decodeReport(report(0x01, 0x22, {0x00, 0xD7}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sensorType, 0x01);
    EXPECT_EQ(decoded->scale, 0);
    EXPECT_EQ(decoded->precision, 1);
    EXPECT_EQ(decoded->value, 215);
}

TEST(SensorMultilevel, AirTemperatureFahrenheit)
{
    // scale=1 (°F), precision=1, size=2, value=706 → 70.6 °F
    const auto decoded = SensorMultilevel::decodeReport(report(0x01, 0x2A, {0x02, 0xC2}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->scale, 1);
    EXPECT_EQ(decoded->precision, 1);
    EXPECT_EQ(decoded->value, 706);
}

TEST(SensorMultilevel, LuminanceLux)
{
    // type=0x03 (luminance), scale=1 (lux), precision=0, size=2, value=1000
    const auto decoded = SensorMultilevel::decodeReport(report(0x03, 0x0A, {0x03, 0xE8}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sensorType, 0x03);
    EXPECT_EQ(decoded->scale, 1);
    EXPECT_EQ(decoded->precision, 0);
    EXPECT_EQ(decoded->value, 1000);
}

TEST(SensorMultilevel, NegativeTemperatureSignExtends)
{
    // precision=1, size=2, value=-15 (0xFFF1) → -1.5 °C
    const auto decoded = SensorMultilevel::decodeReport(report(0x01, 0x22, {0xFF, 0xF1}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, -15);
}

TEST(SensorMultilevel, SingleByteSignedValue)
{
    // size=1, precision=0, value=-1 (0xFF)
    const auto decoded = SensorMultilevel::decodeReport(report(0x01, 0x01, {0xFF}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, -1);
}

TEST(SensorMultilevel, RejectsInvalidSizeAndMalformed)
{
    // size field 3 is invalid (only 1/2/4 allowed)
    EXPECT_FALSE(SensorMultilevel::decodeReport(report(0x01, 0x03, {0x00, 0x01, 0x02})).has_value());
    // truncated: flag says size 4 but only 2 value bytes present
    EXPECT_FALSE(SensorMultilevel::decodeReport(report(0x01, 0x04, {0x00, 0x01})).has_value());
    // wrong command class
    EXPECT_FALSE(SensorMultilevel::decodeReport({0x80, 0x05, 0x01, 0x22, 0x00, 0xD7}).has_value());
    // wrong command byte (Get, not Report)
    EXPECT_FALSE(
        SensorMultilevel::decodeReport({SensorMultilevel::COMMAND_CLASS, 0x04, 0x01, 0x22, 0x00, 0xD7}).has_value());
}
