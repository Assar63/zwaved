#include "Association.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_ASSOCIATION       = 0x85;
constexpr std::uint8_t CMD_SET              = 0x01;
constexpr std::uint8_t CMD_GET              = 0x02;
constexpr std::uint8_t CMD_REPORT           = 0x03;
constexpr std::uint8_t CMD_REMOVE           = 0x04;
constexpr std::uint8_t CMD_GROUPINGS_GET    = 0x05;
constexpr std::uint8_t CMD_GROUPINGS_REPORT = 0x06;
}  // namespace

TEST(Association, EncodeSetSingleMember)
{
    const std::array<std::uint8_t, 1> members{0x05};
    const std::vector<std::uint8_t> expected{CC_ASSOCIATION, CMD_SET, 0x01, 0x05};
    EXPECT_EQ(Association::encodeSet(0x01, std::span<const std::uint8_t>(members)), expected);
}

TEST(Association, EncodeSetMultipleMembers)
{
    const std::array<std::uint8_t, 3> members{0x02, 0x07, 0x0B};
    const std::vector<std::uint8_t> expected{CC_ASSOCIATION, CMD_SET, 0x01, 0x02, 0x07, 0x0B};
    EXPECT_EQ(Association::encodeSet(0x01, std::span<const std::uint8_t>(members)), expected);
}

TEST(Association, EncodeRemoveAllMembers)
{
    // Per spec: empty member list means "remove all members from group".
    const std::span<const std::uint8_t> empty;
    const std::vector<std::uint8_t> expected{CC_ASSOCIATION, CMD_REMOVE, 0x02};
    EXPECT_EQ(Association::encodeRemove(0x02, empty), expected);
}

TEST(Association, EncodeRemoveSpecificMembers)
{
    const std::array<std::uint8_t, 2> members{0x03, 0x09};
    const std::vector<std::uint8_t> expected{CC_ASSOCIATION, CMD_REMOVE, 0x02, 0x03, 0x09};
    EXPECT_EQ(Association::encodeRemove(0x02, std::span<const std::uint8_t>(members)), expected);
}

TEST(Association, EncodeGet)
{
    const std::vector<std::uint8_t> expected{CC_ASSOCIATION, CMD_GET, 0x01};
    EXPECT_EQ(Association::encodeGet(0x01), expected);
}

TEST(Association, EncodeGroupingsGet)
{
    const std::vector<std::uint8_t> expected{CC_ASSOCIATION, CMD_GROUPINGS_GET};
    EXPECT_EQ(Association::encodeGroupingsGet(), expected);
}

TEST(Association, DecodeReportEmptyGroup)
{
    const std::array<std::uint8_t, 5> bytes{CC_ASSOCIATION, CMD_REPORT, 0x01, 0x05, 0x00};
    const auto report = Association::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->groupId, 0x01);
    EXPECT_EQ(report->maxSupported, 0x05);
    EXPECT_EQ(report->reportsToFollow, 0x00);
    EXPECT_TRUE(report->members.empty());
}

TEST(Association, DecodeReportWithMembers)
{
    const std::array<std::uint8_t, 7> bytes{CC_ASSOCIATION, CMD_REPORT, 0x01, 0x05, 0x00, 0x03, 0x07};
    const auto report = Association::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->groupId, 0x01);
    EXPECT_EQ(report->maxSupported, 0x05);
    EXPECT_EQ(report->reportsToFollow, 0x00);
    const std::vector<std::uint8_t> expectedMembers{0x03, 0x07};
    EXPECT_EQ(report->members, expectedMembers);
}

TEST(Association, DecodeReportPropagatesReportsToFollow)
{
    // Multi-frame report: this isn't the last; reportsToFollow > 0.
    const std::array<std::uint8_t, 6> bytes{CC_ASSOCIATION, CMD_REPORT, 0x02, 0x05, 0x01, 0x04};
    const auto report = Association::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->reportsToFollow, 0x01);
    const std::vector<std::uint8_t> expectedMembers{0x04};
    EXPECT_EQ(report->members, expectedMembers);
}

TEST(Association, DecodeReportRejectsTooShort)
{
    const std::array<std::uint8_t, 4> bytes{CC_ASSOCIATION, CMD_REPORT, 0x01, 0x05};
    EXPECT_FALSE(Association::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Association, DecodeReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 5> bytes{0x86, CMD_REPORT, 0x01, 0x05, 0x00};
    EXPECT_FALSE(Association::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Association, DecodeGroupingsReport)
{
    const std::array<std::uint8_t, 3> bytes{CC_ASSOCIATION, CMD_GROUPINGS_REPORT, 0x05};
    const auto report = Association::decodeGroupingsReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->supportedGroupings, 0x05);
}

TEST(Association, DecodeGroupingsReportRejectsTooShort)
{
    const std::array<std::uint8_t, 2> bytes{CC_ASSOCIATION, CMD_GROUPINGS_REPORT};
    EXPECT_FALSE(Association::decodeGroupingsReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Association, DecodeGroupingsReportRejectsWrongCommand)
{
    const std::array<std::uint8_t, 3> bytes{CC_ASSOCIATION, CMD_REPORT, 0x05};
    EXPECT_FALSE(Association::decodeGroupingsReport(std::span<const std::uint8_t>(bytes)).has_value());
}
