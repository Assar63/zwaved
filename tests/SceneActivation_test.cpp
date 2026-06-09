#include "SceneActivation.hpp"

#include <array>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_SCENE_ACTIVATION = 0x2B;
constexpr std::uint8_t CMD_SET             = 0x01;
}  // namespace

TEST(SceneActivation, DecodeSetWithDuration)
{
    // scene 3, dimming duration 5 seconds.
    const std::array<std::uint8_t, 4> bytes{CC_SCENE_ACTIVATION, CMD_SET, 0x03, 0x05};
    const auto decoded = SceneActivation::decodeSet(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sceneId, 3);
    EXPECT_EQ(decoded->dimmingDuration, 5);
}

TEST(SceneActivation, DecodeSetWithoutDurationDefaultsZero)
{
    // v1 wire form omits the duration byte.
    const std::array<std::uint8_t, 3> bytes{CC_SCENE_ACTIVATION, CMD_SET, 0x07};
    const auto decoded = SceneActivation::decodeSet(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sceneId, 7);
    EXPECT_EQ(decoded->dimmingDuration, 0);
}

TEST(SceneActivation, RejectsMalformed)
{
    // Too short (missing scene id).
    const std::array<std::uint8_t, 2> tooShort{CC_SCENE_ACTIVATION, CMD_SET};
    EXPECT_FALSE(SceneActivation::decodeSet(std::span<const std::uint8_t>(tooShort)).has_value());
    // Wrong command class.
    const std::array<std::uint8_t, 3> wrongCc{0x25, CMD_SET, 0x03};
    EXPECT_FALSE(SceneActivation::decodeSet(std::span<const std::uint8_t>(wrongCc)).has_value());
    // Wrong command byte.
    const std::array<std::uint8_t, 3> wrongCmd{CC_SCENE_ACTIVATION, 0x02, 0x03};
    EXPECT_FALSE(SceneActivation::decodeSet(std::span<const std::uint8_t>(wrongCmd)).has_value());
}
