#include "WakeUp.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_WAKE_UP                  = 0x84;
constexpr std::uint8_t CMD_SET                     = 0x04;
constexpr std::uint8_t CMD_GET                     = 0x05;
constexpr std::uint8_t CMD_REPORT                  = 0x06;
constexpr std::uint8_t CMD_NOTIFICATION            = 0x07;
constexpr std::uint8_t CMD_NO_MORE_INFORMATION     = 0x08;
constexpr std::uint8_t CONTROLLER_NODE_ID          = 0x01;
constexpr std::uint32_t INTERVAL_ONE_HOUR_SECONDS  = 3600;      // 0x00 00 0E 10
constexpr std::uint32_t INTERVAL_ONE_DAY_SECONDS   = 86400;     // 0x00 01 51 80
constexpr std::uint32_t INTERVAL_MAX_24BIT_SECONDS = 0xFFFFFF;  // ~194 days
}  // namespace

TEST(WakeUp, EncodeIntervalSetOneHour)
{
    // 3600 seconds = 0x00 0E 10 — big-endian on the wire.
    const std::vector<std::uint8_t> expected{CC_WAKE_UP, CMD_SET, 0x00, 0x0E, 0x10, CONTROLLER_NODE_ID};
    EXPECT_EQ(WakeUp::encodeIntervalSet(INTERVAL_ONE_HOUR_SECONDS, CONTROLLER_NODE_ID), expected);
}

TEST(WakeUp, EncodeIntervalSetOneDay)
{
    // 86400 seconds = 0x01 51 80 — exercises every byte of the BE u24.
    const std::vector<std::uint8_t> expected{CC_WAKE_UP, CMD_SET, 0x01, 0x51, 0x80, CONTROLLER_NODE_ID};
    EXPECT_EQ(WakeUp::encodeIntervalSet(INTERVAL_ONE_DAY_SECONDS, CONTROLLER_NODE_ID), expected);
}

TEST(WakeUp, EncodeIntervalSetClampsTo24Bits)
{
    // A caller passing > 0xFFFFFF must not silently truncate — the
    // encoder clamps so the resulting interval is the spec maximum
    // rather than `requested & 0xFFFFFF` (which for 0x01000000 would
    // be 0, i.e. stay-awake — almost certainly NOT what was meant).
    const std::vector<std::uint8_t> expected{CC_WAKE_UP, CMD_SET, 0xFF, 0xFF, 0xFF, CONTROLLER_NODE_ID};
    EXPECT_EQ(WakeUp::encodeIntervalSet(INTERVAL_MAX_24BIT_SECONDS + 1, CONTROLLER_NODE_ID), expected);
}

TEST(WakeUp, EncodeGet)
{
    const std::vector<std::uint8_t> expected{CC_WAKE_UP, CMD_GET};
    EXPECT_EQ(WakeUp::encodeGet(), expected);
}

TEST(WakeUp, EncodeNoMoreInformation)
{
    // The "go back to sleep" frame the daemon fires after draining
    // a woken node's pending-command queue (#68).
    const std::vector<std::uint8_t> expected{CC_WAKE_UP, CMD_NO_MORE_INFORMATION};
    EXPECT_EQ(WakeUp::encodeNoMoreInformation(), expected);
}

TEST(WakeUp, DecodeIntervalReportOneHour)
{
    const std::array<std::uint8_t, 6> bytes{CC_WAKE_UP, CMD_REPORT, 0x00, 0x0E, 0x10, CONTROLLER_NODE_ID};
    const auto report = WakeUp::decodeIntervalReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->seconds, INTERVAL_ONE_HOUR_SECONDS);
    EXPECT_EQ(report->controllerNodeId, CONTROLLER_NODE_ID);
}

TEST(WakeUp, DecodeIntervalReportMax24Bit)
{
    // 0xFFFFFF in every byte exercises the high/mid/low split of
    // the BE u24 decoder.
    const std::array<std::uint8_t, 6> bytes{CC_WAKE_UP, CMD_REPORT, 0xFF, 0xFF, 0xFF, CONTROLLER_NODE_ID};
    const auto report = WakeUp::decodeIntervalReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->seconds, INTERVAL_MAX_24BIT_SECONDS);
}

TEST(WakeUp, DecodeIntervalReportRejectsTooShort)
{
    // Five bytes: missing controllerNodeId.
    const std::array<std::uint8_t, 5> bytes{CC_WAKE_UP, CMD_REPORT, 0x00, 0x0E, 0x10};
    EXPECT_FALSE(WakeUp::decodeIntervalReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(WakeUp, DecodeIntervalReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 6> bytes{0x25, CMD_REPORT, 0x00, 0x0E, 0x10, CONTROLLER_NODE_ID};
    EXPECT_FALSE(WakeUp::decodeIntervalReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(WakeUp, DecodeIntervalReportRejectsWrongCommand)
{
    // Notification (0x07) is not an Interval Report — refuse so a
    // notification doesn't get misparsed as an interval update with
    // bogus values read out of whatever bytes the notification carries.
    const std::array<std::uint8_t, 6> bytes{CC_WAKE_UP, CMD_NOTIFICATION, 0x00, 0x0E, 0x10, CONTROLLER_NODE_ID};
    EXPECT_FALSE(WakeUp::decodeIntervalReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(WakeUp, DecodeNotificationHappyPath)
{
    const std::array<std::uint8_t, 2> bytes{CC_WAKE_UP, CMD_NOTIFICATION};
    const auto notification = WakeUp::decodeNotification(std::span<const std::uint8_t>(bytes));
    EXPECT_TRUE(notification.has_value());
}

TEST(WakeUp, DecodeNotificationRejectsWrongCommand)
{
    // Interval Report bytes shouldn't be mistaken for a Notification.
    const std::array<std::uint8_t, 6> bytes{CC_WAKE_UP, CMD_REPORT, 0x00, 0x0E, 0x10, CONTROLLER_NODE_ID};
    EXPECT_FALSE(WakeUp::decodeNotification(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(WakeUp, DecodeNotificationRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 2> bytes{0x25, CMD_NOTIFICATION};
    EXPECT_FALSE(WakeUp::decodeNotification(std::span<const std::uint8_t>(bytes)).has_value());
}
