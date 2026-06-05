#include "UserCode.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const std::vector<std::uint8_t> PIN_1234{'1', '2', '3', '4'};
}  // namespace

TEST(UserCode, EncodeGet)
{
    const std::vector<std::uint8_t> expected{UserCode::COMMAND_CLASS, UserCode::USER_CODE_GET, 3};
    EXPECT_EQ(UserCode::encodeGet(3), expected);
}

TEST(UserCode, EncodeUsersNumberGet)
{
    const std::vector<std::uint8_t> expected{UserCode::COMMAND_CLASS, UserCode::USER_CODE_USERS_NUMBER_GET};
    EXPECT_EQ(UserCode::encodeUsersNumberGet(), expected);
}

TEST(UserCode, EncodeSetCarriesCode)
{
    const auto frame = UserCode::encodeSet(2, UserCode::STATUS_OCCUPIED, PIN_1234);
    const std::vector<std::uint8_t> expected{
        UserCode::COMMAND_CLASS, UserCode::USER_CODE_SET, 2, UserCode::STATUS_OCCUPIED, '1', '2', '3', '4'};
    EXPECT_EQ(frame, expected);
}

TEST(UserCode, DecodeReportWithCode)
{
    std::vector<std::uint8_t> frame{
        UserCode::COMMAND_CLASS, UserCode::USER_CODE_REPORT, 2, UserCode::STATUS_OCCUPIED, '1', '2', '3', '4'};
    const auto decoded = UserCode::decodeReport(frame);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->userIdentifier, 2);
    EXPECT_EQ(decoded->userIdStatus, UserCode::STATUS_OCCUPIED);
    EXPECT_EQ(decoded->userCode, PIN_1234);
}

TEST(UserCode, DecodeReportEmptySlot)
{
    // Available slot: status 0x00, no code bytes.
    const std::vector<std::uint8_t> frame{
        UserCode::COMMAND_CLASS, UserCode::USER_CODE_REPORT, 5, UserCode::STATUS_AVAILABLE};
    const auto decoded = UserCode::decodeReport(frame);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->userIdStatus, UserCode::STATUS_AVAILABLE);
    EXPECT_TRUE(decoded->userCode.empty());
}

TEST(UserCode, DecodeUsersNumberReport)
{
    const std::vector<std::uint8_t> frame{UserCode::COMMAND_CLASS, UserCode::USER_CODE_USERS_NUMBER_REPORT, 30};
    const auto decoded = UserCode::decodeUsersNumberReport(frame);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->supportedUsers, 30);
}

TEST(UserCode, RejectsMalformed)
{
    // Report too short (no status byte).
    const std::vector<std::uint8_t> shortReport{UserCode::COMMAND_CLASS, UserCode::USER_CODE_REPORT, 1};
    EXPECT_FALSE(UserCode::decodeReport(shortReport).has_value());
    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{0x62, UserCode::USER_CODE_REPORT, 1, 0x01};
    EXPECT_FALSE(UserCode::decodeReport(wrongCc).has_value());
    // UsersNumber report too short.
    const std::vector<std::uint8_t> shortNum{UserCode::COMMAND_CLASS, UserCode::USER_CODE_USERS_NUMBER_REPORT};
    EXPECT_FALSE(UserCode::decodeUsersNumberReport(shortNum).has_value());
}
