#include "CentralScene.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// Build a Central Scene Notification: CC + cmd + seq + properties + scene.
auto notification(std::uint8_t sequence, std::uint8_t properties, std::uint8_t scene) -> std::vector<std::uint8_t>
{
    return {CentralScene::COMMAND_CLASS, CentralScene::CENTRAL_SCENE_NOTIFICATION, sequence, properties, scene};
}
}  // namespace

TEST(CentralScene, DecodeSinglePress)
{
    // seq=10, keyAttribute=0 (press 1x), scene=1.
    const auto decoded = CentralScene::decodeNotification(notification(10, CentralScene::KEY_PRESS_1X, 1));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->sequenceNumber, 10);
    EXPECT_EQ(decoded->keyAttribute, CentralScene::KEY_PRESS_1X);
    EXPECT_EQ(decoded->sceneNumber, 1);
    EXPECT_FALSE(decoded->slowRefresh);
}

TEST(CentralScene, DecodeDoubleTapOnScene2)
{
    const auto decoded = CentralScene::decodeNotification(notification(11, CentralScene::KEY_PRESS_2X, 2));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->keyAttribute, CentralScene::KEY_PRESS_2X);
    EXPECT_EQ(decoded->sceneNumber, 2);
}

TEST(CentralScene, DecodeMasksKeyAttributeAndSlowRefresh)
{
    // properties 0x82 = slowRefresh flag (0x80) | keyAttribute 2 (hold).
    const auto decoded = CentralScene::decodeNotification(notification(12, 0x82, 3));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->keyAttribute, CentralScene::KEY_HELD);
    EXPECT_TRUE(decoded->slowRefresh);
    EXPECT_EQ(decoded->sceneNumber, 3);
}

TEST(CentralScene, RejectsMalformed)
{
    // Too short (missing scene byte).
    const std::vector<std::uint8_t> tooShort{
        CentralScene::COMMAND_CLASS, CentralScene::CENTRAL_SCENE_NOTIFICATION, 0x01, 0x00};
    EXPECT_FALSE(CentralScene::decodeNotification(tooShort).has_value());

    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{0x25, CentralScene::CENTRAL_SCENE_NOTIFICATION, 0x01, 0x00, 0x01};
    EXPECT_FALSE(CentralScene::decodeNotification(wrongCc).has_value());

    // Wrong command byte (Supported Get, not Notification).
    const std::vector<std::uint8_t> wrongCmd{CentralScene::COMMAND_CLASS, 0x01, 0x01, 0x00, 0x01};
    EXPECT_FALSE(CentralScene::decodeNotification(wrongCmd).has_value());
}
