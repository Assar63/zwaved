#include "Supervision.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_SUPERVISION = 0x6C;
constexpr std::uint8_t CMD_GET        = 0x01;
constexpr std::uint8_t CMD_REPORT     = 0x02;
constexpr std::uint8_t STATUS_SUCCESS = 0xFF;
}  // namespace

TEST(Supervision, EncodeGetWrapsInnerWithSessionAndLength)
{
    // session 5, no updates, inner = Binary Switch Set ON (0x25 0x01 0xFF).
    const std::array<std::uint8_t, 3> inner{0x25, 0x01, 0xFF};
    const auto frame = Supervision::encodeGet(5, false, std::span<const std::uint8_t>(inner));
    // CC, GET, properties(=session 5), inner-length(=3), then inner.
    EXPECT_EQ(frame, (std::vector<std::uint8_t>{CC_SUPERVISION, CMD_GET, 0x05, 0x03, 0x25, 0x01, 0xFF}));
}

TEST(Supervision, EncodeGetSetsMoreUpdatesFlagAndMasksSession)
{
    const std::array<std::uint8_t, 1> inner{0x00};
    // session 0x45 -> masked to 0x05; requestUpdates -> bit 7 set => 0x85.
    const auto frame = Supervision::encodeGet(0x45, true, std::span<const std::uint8_t>(inner));
    ASSERT_GE(frame.size(), 4U);
    EXPECT_EQ(frame[2], 0x85);
    EXPECT_EQ(frame[3], 0x01);  // inner length
}

TEST(Supervision, DecodeReportExtractsSessionStatusDuration)
{
    // properties 0x85 = more-updates | session 5; status success; duration 0.
    const std::array<std::uint8_t, 5> bytes{CC_SUPERVISION, CMD_REPORT, 0x85, STATUS_SUCCESS, 0x00};
    const auto report = Supervision::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->sessionId, 5);
    EXPECT_TRUE(report->moreStatusUpdates);
    EXPECT_EQ(report->status, STATUS_SUCCESS);
    EXPECT_EQ(report->duration, 0);
}

TEST(Supervision, DecodeReportRejectsMalformed)
{
    // Too short.
    const std::array<std::uint8_t, 4> tooShort{CC_SUPERVISION, CMD_REPORT, 0x05, STATUS_SUCCESS};
    EXPECT_FALSE(Supervision::decodeReport(std::span<const std::uint8_t>(tooShort)).has_value());
    // Wrong command class.
    const std::array<std::uint8_t, 5> wrongCc{0x25, CMD_REPORT, 0x05, STATUS_SUCCESS, 0x00};
    EXPECT_FALSE(Supervision::decodeReport(std::span<const std::uint8_t>(wrongCc)).has_value());
    // Wrong command (GET, not REPORT).
    const std::array<std::uint8_t, 5> wrongCmd{CC_SUPERVISION, CMD_GET, 0x05, STATUS_SUCCESS, 0x00};
    EXPECT_FALSE(Supervision::decodeReport(std::span<const std::uint8_t>(wrongCmd)).has_value());
}

TEST(Supervision, EncodeDecodeRoundTripSession)
{
    const std::array<std::uint8_t, 2> inner{0x25, 0x02};
    const auto frame = Supervision::encodeGet(0x3F, false, std::span<const std::uint8_t>(inner));
    // Re-read the session id out of the GET properties byte (mask).
    ASSERT_GE(frame.size(), 3U);
    EXPECT_EQ(frame[2] & 0x3F, 0x3F);
}
