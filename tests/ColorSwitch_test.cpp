#include "ColorSwitch.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
auto report(std::initializer_list<std::uint8_t> tail) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{ColorSwitch::COMMAND_CLASS, ColorSwitch::SWITCH_COLOR_REPORT};
    out.insert(out.end(), tail);
    return out;
}
}  // namespace

TEST(ColorSwitch, EncodeGet)
{
    const std::vector<std::uint8_t> expected{
        ColorSwitch::COMMAND_CLASS, ColorSwitch::SWITCH_COLOR_GET, ColorSwitch::COMPONENT_RED};
    EXPECT_EQ(ColorSwitch::encodeGet(ColorSwitch::COMPONENT_RED), expected);
}

TEST(ColorSwitch, EncodeSetPacksPairsAndCount)
{
    // red full, green off, blue off; duration default.
    const std::vector<std::uint8_t> components{
        ColorSwitch::COMPONENT_RED, 0xFF, ColorSwitch::COMPONENT_GREEN, 0x00, ColorSwitch::COMPONENT_BLUE, 0x00};
    const auto frame = ColorSwitch::encodeSet(components, 0xFF);
    const std::vector<std::uint8_t> expected{
        ColorSwitch::COMMAND_CLASS, ColorSwitch::SWITCH_COLOR_SET, 3, 0x02, 0xFF, 0x03, 0x00, 0x04, 0x00, 0xFF};
    EXPECT_EQ(frame, expected);
}

TEST(ColorSwitch, EncodeSetIgnoresTrailingOddByte)
{
    // 5 bytes = 2 complete pairs + 1 stray; stray dropped, count = 2.
    const std::vector<std::uint8_t> components{0x02, 0x10, 0x03, 0x20, 0x04};
    const auto frame = ColorSwitch::encodeSet(components, 0x00);
    const std::vector<std::uint8_t> expected{
        ColorSwitch::COMMAND_CLASS, ColorSwitch::SWITCH_COLOR_SET, 2, 0x02, 0x10, 0x03, 0x20, 0x00};
    EXPECT_EQ(frame, expected);
}

TEST(ColorSwitch, DecodeV1MirrorsTarget)
{
    const auto decoded = ColorSwitch::decodeReport(report({ColorSwitch::COMPONENT_RED, 200}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->componentId, ColorSwitch::COMPONENT_RED);
    EXPECT_EQ(decoded->value, 200);
    EXPECT_EQ(decoded->targetValue, 200);  // v1: mirrors value
    EXPECT_EQ(decoded->duration, 0);
}

TEST(ColorSwitch, DecodeV3CarriesTargetAndDuration)
{
    const auto decoded = ColorSwitch::decodeReport(report({ColorSwitch::COMPONENT_GREEN, 100, 255, 5}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->componentId, ColorSwitch::COMPONENT_GREEN);
    EXPECT_EQ(decoded->value, 100);
    EXPECT_EQ(decoded->targetValue, 255);
    EXPECT_EQ(decoded->duration, 5);
}

TEST(ColorSwitch, RejectsMalformed)
{
    // Too short (no value byte).
    EXPECT_FALSE(ColorSwitch::decodeReport(report({ColorSwitch::COMPONENT_RED})).has_value());

    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{0x31, ColorSwitch::SWITCH_COLOR_REPORT, 0x02, 0x10};
    EXPECT_FALSE(ColorSwitch::decodeReport(wrongCc).has_value());

    // Wrong command byte (Get, not Report).
    const std::vector<std::uint8_t> wrongCmd{ColorSwitch::COMMAND_CLASS, ColorSwitch::SWITCH_COLOR_GET, 0x02, 0x10};
    EXPECT_FALSE(ColorSwitch::decodeReport(wrongCmd).has_value());
}
