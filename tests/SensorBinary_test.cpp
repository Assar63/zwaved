#include "SensorBinary.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// Build a v1 Sensor Binary Report payload (CC + cmd + value).
auto reportV1(std::uint8_t value) -> std::vector<std::uint8_t>
{
    return {SensorBinary::COMMAND_CLASS, SensorBinary::SENSOR_BINARY_REPORT, value};
}

// Build a v2 Sensor Binary Report payload (CC + cmd + value + sensorType).
auto reportV2(std::uint8_t value, std::uint8_t sensorType) -> std::vector<std::uint8_t>
{
    return {SensorBinary::COMMAND_CLASS, SensorBinary::SENSOR_BINARY_REPORT, value, sensorType};
}
}  // namespace

TEST(SensorBinary, EncodeGetIsTwoBytes)
{
    const std::vector<std::uint8_t> expected{SensorBinary::COMMAND_CLASS, SensorBinary::SENSOR_BINARY_GET};
    EXPECT_EQ(SensorBinary::encodeGet(), expected);
}

TEST(SensorBinary, V1Idle)
{
    const auto decoded = SensorBinary::decodeReport(reportV1(SensorBinary::VALUE_IDLE));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, SensorBinary::VALUE_IDLE);
    EXPECT_EQ(decoded->sensorType, 0);  // v1 has no type byte
}

TEST(SensorBinary, V1Active)
{
    const auto decoded = SensorBinary::decodeReport(reportV1(SensorBinary::VALUE_ACTIVE));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, SensorBinary::VALUE_ACTIVE);
    EXPECT_EQ(decoded->sensorType, 0);
}

TEST(SensorBinary, V2CarriesSensorType)
{
    // value=active, sensorType=0x0A (motion).
    const auto decoded = SensorBinary::decodeReport(reportV2(SensorBinary::VALUE_ACTIVE, 0x0A));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, SensorBinary::VALUE_ACTIVE);
    EXPECT_EQ(decoded->sensorType, 0x0A);
}

TEST(SensorBinary, RejectsMalformed)
{
    // Too short for even the v1 single-value form.
    const std::vector<std::uint8_t> tooShort{SensorBinary::COMMAND_CLASS, SensorBinary::SENSOR_BINARY_REPORT};
    EXPECT_FALSE(SensorBinary::decodeReport(tooShort).has_value());

    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{0x80, SensorBinary::SENSOR_BINARY_REPORT, 0xFF};
    EXPECT_FALSE(SensorBinary::decodeReport(wrongCc).has_value());

    // Wrong command byte (Get, not Report).
    const std::vector<std::uint8_t> wrongCmd{SensorBinary::COMMAND_CLASS, SensorBinary::SENSOR_BINARY_GET, 0xFF};
    EXPECT_FALSE(SensorBinary::decodeReport(wrongCmd).has_value());
}
