#include "DoorLock.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
auto report(std::initializer_list<std::uint8_t> tail) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{DoorLock::COMMAND_CLASS, DoorLock::DOOR_LOCK_REPORT};
    out.insert(out.end(), tail);
    return out;
}
}  // namespace

TEST(DoorLock, EncodeSetAndGet)
{
    const std::vector<std::uint8_t> set{DoorLock::COMMAND_CLASS, DoorLock::DOOR_LOCK_SET, DoorLock::MODE_SECURED};
    EXPECT_EQ(DoorLock::encodeSet(DoorLock::MODE_SECURED), set);
    const std::vector<std::uint8_t> get{DoorLock::COMMAND_CLASS, DoorLock::DOOR_LOCK_GET};
    EXPECT_EQ(DoorLock::encodeGet(), get);
}

TEST(DoorLock, DecodeV1Operation)
{
    // currentMode=secured, handles=0, condition=0x03, timeout 0xFE/0xFE (n/a).
    const auto decoded = DoorLock::decodeOperationReport(report({DoorLock::MODE_SECURED, 0x00, 0x03, 0xFE, 0xFE}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->currentMode, DoorLock::MODE_SECURED);
    EXPECT_EQ(decoded->condition, 0x03);
    EXPECT_EQ(decoded->lockTimeoutMinutes, 0xFE);
    EXPECT_EQ(decoded->targetMode, 0);  // v1: no transition fields
    EXPECT_EQ(decoded->duration, 0);
}

TEST(DoorLock, DecodeV4TargetAndDuration)
{
    // v4: + targetMode=0xFF (secured), duration=5.
    const auto decoded =
        DoorLock::decodeOperationReport(report({DoorLock::MODE_UNSECURED, 0x00, 0x02, 0x00, 0x00, 0xFF, 0x05}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->currentMode, DoorLock::MODE_UNSECURED);
    EXPECT_EQ(decoded->targetMode, DoorLock::MODE_SECURED);
    EXPECT_EQ(decoded->duration, 5);
}

TEST(DoorLock, RejectsMalformed)
{
    // Too short (only 3 of the 5 v1 body fields).
    EXPECT_FALSE(DoorLock::decodeOperationReport(report({0xFF, 0x00, 0x03})).has_value());
    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{0x25, DoorLock::DOOR_LOCK_REPORT, 0xFF, 0, 0, 0, 0};
    EXPECT_FALSE(DoorLock::decodeOperationReport(wrongCc).has_value());
    // Wrong command byte (Get, not Report).
    const std::vector<std::uint8_t> wrongCmd{DoorLock::COMMAND_CLASS, DoorLock::DOOR_LOCK_GET, 0xFF, 0, 0, 0, 0};
    EXPECT_FALSE(DoorLock::decodeOperationReport(wrongCmd).has_value());
}
