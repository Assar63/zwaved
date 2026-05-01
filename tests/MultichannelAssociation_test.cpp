#include "MultichannelAssociation.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_MC_ASSOCIATION    = 0x8E;
constexpr std::uint8_t CMD_SET              = 0x01;
constexpr std::uint8_t CMD_GET              = 0x02;
constexpr std::uint8_t CMD_REPORT           = 0x03;
constexpr std::uint8_t CMD_REMOVE           = 0x04;
constexpr std::uint8_t CMD_GROUPINGS_GET    = 0x05;
constexpr std::uint8_t CMD_GROUPINGS_REPORT = 0x06;
constexpr std::uint8_t MARKER               = 0x00;
}  // namespace

// ============================================================================
// encode
// ============================================================================

TEST(MultichannelAssociation, EncodeSetWithOnlyNodeMembers)
{
    // Always emit MARKER even when no endpoint members — keeps the
    // wire shape unambiguous on the receiver.
    const std::array<std::uint8_t, 2> nodes{0x05, 0x07};
    const std::vector<std::uint8_t> expected{CC_MC_ASSOCIATION, CMD_SET, 0x01, 0x05, 0x07, MARKER};
    const std::span<const MultichannelAssociation::EndpointMember> noEndpoints;
    EXPECT_EQ(MultichannelAssociation::encodeSet(0x01, std::span<const std::uint8_t>(nodes), noEndpoints), expected);
}

TEST(MultichannelAssociation, EncodeSetWithOnlyEndpointMembers)
{
    const std::array<MultichannelAssociation::EndpointMember, 2> endpoints{
        {{.nodeId = 0x0B, .endpoint = 0x02}, {.nodeId = 0x0B, .endpoint = 0x03}}};
    const std::span<const std::uint8_t> noNodes;
    const std::vector<std::uint8_t> expected{CC_MC_ASSOCIATION, CMD_SET, 0x01, MARKER, 0x0B, 0x02, 0x0B, 0x03};
    EXPECT_EQ(MultichannelAssociation::encodeSet(
                  0x01, noNodes, std::span<const MultichannelAssociation::EndpointMember>(endpoints)),
              expected);
}

TEST(MultichannelAssociation, EncodeSetWithBothLists)
{
    const std::array<std::uint8_t, 1> nodes{0x05};
    const std::array<MultichannelAssociation::EndpointMember, 1> endpoints{{{.nodeId = 0x0B, .endpoint = 0x02}}};
    const std::vector<std::uint8_t> expected{CC_MC_ASSOCIATION, CMD_SET, 0x01, 0x05, MARKER, 0x0B, 0x02};
    EXPECT_EQ(MultichannelAssociation::encodeSet(0x01,
                                                 std::span<const std::uint8_t>(nodes),
                                                 std::span<const MultichannelAssociation::EndpointMember>(endpoints)),
              expected);
}

TEST(MultichannelAssociation, EncodeSetEmptyEmitsMarker)
{
    // Distinct from REMOVE: SET with empty members is a useless ask
    // but still emits the MARKER for shape consistency.
    const std::span<const std::uint8_t> noNodes;
    const std::span<const MultichannelAssociation::EndpointMember> noEndpoints;
    const std::vector<std::uint8_t> expected{CC_MC_ASSOCIATION, CMD_SET, 0x07, MARKER};
    EXPECT_EQ(MultichannelAssociation::encodeSet(0x07, noNodes, noEndpoints), expected);
}

TEST(MultichannelAssociation, EncodeRemoveAllMembers)
{
    // Per spec: REMOVE with no MARKER and no member lists means
    // "remove all members from this group".
    const std::span<const std::uint8_t> noNodes;
    const std::span<const MultichannelAssociation::EndpointMember> noEndpoints;
    const std::vector<std::uint8_t> expected{CC_MC_ASSOCIATION, CMD_REMOVE, 0x02};
    EXPECT_EQ(MultichannelAssociation::encodeRemove(0x02, noNodes, noEndpoints), expected);
}

TEST(MultichannelAssociation, EncodeRemoveSpecificEndpoints)
{
    const std::span<const std::uint8_t> noNodes;
    const std::array<MultichannelAssociation::EndpointMember, 1> endpoints{{{.nodeId = 0x0B, .endpoint = 0x04}}};
    const std::vector<std::uint8_t> expected{CC_MC_ASSOCIATION, CMD_REMOVE, 0x02, MARKER, 0x0B, 0x04};
    EXPECT_EQ(MultichannelAssociation::encodeRemove(
                  0x02, noNodes, std::span<const MultichannelAssociation::EndpointMember>(endpoints)),
              expected);
}

TEST(MultichannelAssociation, EncodeRemoveSpecificNodesEmitsMarker)
{
    const std::array<std::uint8_t, 2> nodes{0x03, 0x09};
    const std::span<const MultichannelAssociation::EndpointMember> noEndpoints;
    const std::vector<std::uint8_t> expected{CC_MC_ASSOCIATION, CMD_REMOVE, 0x02, 0x03, 0x09, MARKER};
    EXPECT_EQ(MultichannelAssociation::encodeRemove(0x02, std::span<const std::uint8_t>(nodes), noEndpoints), expected);
}

TEST(MultichannelAssociation, EncodeGet)
{
    const std::vector<std::uint8_t> expected{CC_MC_ASSOCIATION, CMD_GET, 0x01};
    EXPECT_EQ(MultichannelAssociation::encodeGet(0x01), expected);
}

TEST(MultichannelAssociation, EncodeGroupingsGet)
{
    const std::vector<std::uint8_t> expected{CC_MC_ASSOCIATION, CMD_GROUPINGS_GET};
    EXPECT_EQ(MultichannelAssociation::encodeGroupingsGet(), expected);
}

// ============================================================================
// decode REPORT
// ============================================================================

TEST(MultichannelAssociation, DecodeReportEmptyGroup)
{
    const std::array<std::uint8_t, 5> bytes{CC_MC_ASSOCIATION, CMD_REPORT, 0x01, 0x05, 0x00};
    const auto report = MultichannelAssociation::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->groupId, 0x01);
    EXPECT_EQ(report->maxSupported, 0x05);
    EXPECT_EQ(report->reportsToFollow, 0x00);
    EXPECT_TRUE(report->nodeMembers.empty());
    EXPECT_TRUE(report->endpointMembers.empty());
}

TEST(MultichannelAssociation, DecodeReportNodeMembersOnly)
{
    // Header (5) + node members (2) + MARKER (1) — endpoint segment empty.
    const std::array<std::uint8_t, 8> bytes{CC_MC_ASSOCIATION, CMD_REPORT, 0x01, 0x05, 0x00, 0x03, 0x07, MARKER};
    const auto report = MultichannelAssociation::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    const std::vector<std::uint8_t> expectedNodes{0x03, 0x07};
    EXPECT_EQ(report->nodeMembers, expectedNodes);
    EXPECT_TRUE(report->endpointMembers.empty());
}

TEST(MultichannelAssociation, DecodeReportEndpointMembersOnly)
{
    // Header + MARKER + (nodeId, endpoint) pair.
    const std::array<std::uint8_t, 8> bytes{CC_MC_ASSOCIATION, CMD_REPORT, 0x01, 0x05, 0x00, MARKER, 0x0B, 0x02};
    const auto report = MultichannelAssociation::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->nodeMembers.empty());
    ASSERT_EQ(report->endpointMembers.size(), 1U);
    EXPECT_EQ(report->endpointMembers[0].nodeId, 0x0B);
    EXPECT_EQ(report->endpointMembers[0].endpoint, 0x02);
}

TEST(MultichannelAssociation, DecodeReportBothLists)
{
    const std::array<std::uint8_t, 11> bytes{
        CC_MC_ASSOCIATION, CMD_REPORT, 0x01, 0x05, 0x00, 0x03, 0x07, MARKER, 0x0B, 0x02, 0x0B};
    // The trailing 0x0B starts a (nodeId, endpoint) pair without its
    // partner — odd byte count in the endpoint segment is malformed.
    EXPECT_FALSE(MultichannelAssociation::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(MultichannelAssociation, DecodeReportBothListsValid)
{
    const std::array<std::uint8_t, 12> bytes{
        CC_MC_ASSOCIATION, CMD_REPORT, 0x01, 0x05, 0x00, 0x03, 0x07, MARKER, 0x0B, 0x02, 0x0B, 0x03};
    const auto report = MultichannelAssociation::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    const std::vector<std::uint8_t> expectedNodes{0x03, 0x07};
    EXPECT_EQ(report->nodeMembers, expectedNodes);
    ASSERT_EQ(report->endpointMembers.size(), 2U);
    EXPECT_EQ(report->endpointMembers[0].nodeId, 0x0B);
    EXPECT_EQ(report->endpointMembers[0].endpoint, 0x02);
    EXPECT_EQ(report->endpointMembers[1].nodeId, 0x0B);
    EXPECT_EQ(report->endpointMembers[1].endpoint, 0x03);
}

TEST(MultichannelAssociation, DecodeReportNoMarkerTreatsAllAsNodes)
{
    // Spec: MARKER MAY be omitted when there are no endpoint
    // associations. The decoder then treats every byte after the
    // header as a node member.
    const std::array<std::uint8_t, 7> bytes{CC_MC_ASSOCIATION, CMD_REPORT, 0x01, 0x05, 0x00, 0x03, 0x07};
    const auto report = MultichannelAssociation::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    const std::vector<std::uint8_t> expectedNodes{0x03, 0x07};
    EXPECT_EQ(report->nodeMembers, expectedNodes);
    EXPECT_TRUE(report->endpointMembers.empty());
}

TEST(MultichannelAssociation, DecodeReportPropagatesReportsToFollow)
{
    const std::array<std::uint8_t, 6> bytes{CC_MC_ASSOCIATION, CMD_REPORT, 0x02, 0x05, 0x01, 0x04};
    const auto report = MultichannelAssociation::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->reportsToFollow, 0x01);
}

TEST(MultichannelAssociation, DecodeReportRejectsTooShort)
{
    const std::array<std::uint8_t, 4> bytes{CC_MC_ASSOCIATION, CMD_REPORT, 0x01, 0x05};
    EXPECT_FALSE(MultichannelAssociation::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(MultichannelAssociation, DecodeReportRejectsWrongCommandClass)
{
    // Plain Association (0x85), not Multi Channel.
    const std::array<std::uint8_t, 5> bytes{0x85, CMD_REPORT, 0x01, 0x05, 0x00};
    EXPECT_FALSE(MultichannelAssociation::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

// ============================================================================
// decode GROUPINGS_REPORT
// ============================================================================

TEST(MultichannelAssociation, DecodeGroupingsReport)
{
    const std::array<std::uint8_t, 3> bytes{CC_MC_ASSOCIATION, CMD_GROUPINGS_REPORT, 0x05};
    const auto report = MultichannelAssociation::decodeGroupingsReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->supportedGroupings, 0x05);
}

TEST(MultichannelAssociation, DecodeGroupingsReportRejectsTooShort)
{
    const std::array<std::uint8_t, 2> bytes{CC_MC_ASSOCIATION, CMD_GROUPINGS_REPORT};
    EXPECT_FALSE(MultichannelAssociation::decodeGroupingsReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(MultichannelAssociation, DecodeGroupingsReportRejectsWrongCommand)
{
    const std::array<std::uint8_t, 3> bytes{CC_MC_ASSOCIATION, CMD_REPORT, 0x05};
    EXPECT_FALSE(MultichannelAssociation::decodeGroupingsReport(std::span<const std::uint8_t>(bytes)).has_value());
}
