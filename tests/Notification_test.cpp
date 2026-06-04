#include "Notification.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// Build a v3 Notification Report payload. The 9-byte header is:
//   CC + cmd + v1AlarmType + v1AlarmLevel + reserved + notificationStatus
//   + notificationType + event + properties(paramLength), followed by the
//   event-parameter bytes. `paramBytes` is appended verbatim and its length
//   is written into the low 5 bits of the properties byte.
auto report(std::uint8_t notificationType,
            std::uint8_t event,
            std::uint8_t status,
            std::initializer_list<std::uint8_t> paramBytes) -> std::vector<std::uint8_t>
{
    const auto paramLength = static_cast<std::uint8_t>(paramBytes.size());
    std::vector<std::uint8_t> out{Notification::COMMAND_CLASS,
                                  Notification::NOTIFICATION_REPORT,
                                  0x00,    // v1 alarm type
                                  0x00,    // v1 alarm level
                                  0x00,    // reserved
                                  status,  // notificationStatus
                                  notificationType,
                                  event,
                                  static_cast<std::uint8_t>(paramLength & 0x1F)};
    out.insert(out.end(), paramBytes);
    return out;
}
}  // namespace

TEST(Notification, EncodeGetIsTypeScoped)
{
    // Home Security (0x07) → CC, Get, v1AlarmType=0, type, eventFilter=0.
    const auto frame = Notification::encodeGet(0x07);
    const std::vector<std::uint8_t> expected{
        Notification::COMMAND_CLASS, Notification::NOTIFICATION_GET, 0x00, 0x07, 0x00};
    EXPECT_EQ(frame, expected);
}

TEST(Notification, HomeSecurityMotionNoParams)
{
    // type=0x07 (Home Security), event=0x08 (motion detected), no params.
    const auto decoded = Notification::decodeReport(report(0x07, 0x08, 0xFF, {}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->notificationType, 0x07);
    EXPECT_EQ(decoded->event, 0x08);
    EXPECT_EQ(decoded->status, 0xFF);
    EXPECT_TRUE(decoded->parameters.empty());
}

TEST(Notification, WaterLeakNoParams)
{
    // type=0x05 (Water), event=0x02 (water leak detected, unknown location).
    const auto decoded = Notification::decodeReport(report(0x05, 0x02, 0xFF, {}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->notificationType, 0x05);
    EXPECT_EQ(decoded->event, 0x02);
    EXPECT_TRUE(decoded->parameters.empty());
}

TEST(Notification, AccessControlWithEventParameters)
{
    // type=0x06 (Access Control), event=0x05 (window/door open) with two
    // event-parameter bytes that must be carried through verbatim.
    const auto decoded = Notification::decodeReport(report(0x06, 0x05, 0xFF, {0xAB, 0xCD}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->notificationType, 0x06);
    EXPECT_EQ(decoded->event, 0x05);
    const std::vector<std::uint8_t> expectedParams{0xAB, 0xCD};
    EXPECT_EQ(decoded->parameters, expectedParams);
}

TEST(Notification, RejectsMalformed)
{
    // Truncated: properties byte declares 3 param bytes but none present.
    const std::vector<std::uint8_t> truncated{
        Notification::COMMAND_CLASS, Notification::NOTIFICATION_REPORT, 0x00, 0x00, 0x00, 0xFF, 0x07, 0x08, 0x03};
    EXPECT_FALSE(Notification::decodeReport(truncated).has_value());

    // Shorter than the 9-byte header.
    const std::vector<std::uint8_t> tooShort{Notification::COMMAND_CLASS, Notification::NOTIFICATION_REPORT, 0x00};
    EXPECT_FALSE(Notification::decodeReport(tooShort).has_value());

    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{
        0x80, Notification::NOTIFICATION_REPORT, 0x00, 0x00, 0x00, 0xFF, 0x07, 0x08, 0x00};
    EXPECT_FALSE(Notification::decodeReport(wrongCc).has_value());

    // Wrong command byte (Get, not Report).
    const std::vector<std::uint8_t> wrongCmd{
        Notification::COMMAND_CLASS, Notification::NOTIFICATION_GET, 0x00, 0x00, 0x00, 0xFF, 0x07, 0x08, 0x00};
    EXPECT_FALSE(Notification::decodeReport(wrongCmd).has_value());
}
