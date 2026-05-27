#include "ZWavePlusInfo.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_ZWAVEPLUS_INFO = 0x5E;
constexpr std::uint8_t CMD_GET           = 0x01;
constexpr std::uint8_t CMD_REPORT        = 0x02;

constexpr std::uint8_t VERSION_V2           = 0x02;
constexpr std::uint8_t ROLE_ALWAYS_ON_SLAVE = 0x05;
constexpr std::uint8_t NODE_TYPE_ZWAVE_PLUS = 0x00;

// Z-Wave alliance icon types for a typical wall switch.
constexpr std::uint16_t INSTALLER_ICON_GENERIC_WALL_SWITCH = 0x0700;
constexpr std::uint16_t USER_ICON_BINARY_WALL_SWITCH       = 0x0701;
}  // namespace

TEST(ZWavePlusInfo, EncodeGet)
{
    // Z-Wave Plus Info is unusual: GET = 0x01, REPORT = 0x02 (most
    // CCs use 0x02/0x03 or 0x04/0x05). The encoded GET frame must
    // therefore be CC + 0x01 — verifying here so a typo in the
    // manifest's byte assignments would fail loudly.
    const std::vector<std::uint8_t> expected{CC_ZWAVEPLUS_INFO, CMD_GET};
    EXPECT_EQ(ZWavePlusInfo::encodeGet(), expected);
}

TEST(ZWavePlusInfo, DecodeReportAlwaysOnSlaveWallSwitch)
{
    const std::array<std::uint8_t, 9> bytes{
        CC_ZWAVEPLUS_INFO, CMD_REPORT, VERSION_V2, ROLE_ALWAYS_ON_SLAVE, NODE_TYPE_ZWAVE_PLUS, 0x07, 0x00, 0x07, 0x01};
    const auto report = ZWavePlusInfo::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->zwavePlusVersion, VERSION_V2);
    EXPECT_EQ(report->roleType, ROLE_ALWAYS_ON_SLAVE);
    EXPECT_EQ(report->nodeType, NODE_TYPE_ZWAVE_PLUS);
    EXPECT_EQ(report->installerIconType, INSTALLER_ICON_GENERIC_WALL_SWITCH);
    EXPECT_EQ(report->userIconType, USER_ICON_BINARY_WALL_SWITCH);
}

TEST(ZWavePlusInfo, DecodeReportMaxIconValues)
{
    // 0xFFFF in both icon slots exercises the high/low byte split
    // of the BE u16 decoder.
    const std::array<std::uint8_t, 9> bytes{
        CC_ZWAVEPLUS_INFO, CMD_REPORT, VERSION_V2, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    const auto report = ZWavePlusInfo::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->installerIconType, 0xFFFFU);
    EXPECT_EQ(report->userIconType, 0xFFFFU);
}

TEST(ZWavePlusInfo, DecodeReportRejectsTooShort)
{
    // Eight bytes: missing the last byte of userIconType.
    const std::array<std::uint8_t, 8> bytes{
        CC_ZWAVEPLUS_INFO, CMD_REPORT, VERSION_V2, ROLE_ALWAYS_ON_SLAVE, NODE_TYPE_ZWAVE_PLUS, 0x07, 0x00, 0x07};
    EXPECT_FALSE(ZWavePlusInfo::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(ZWavePlusInfo, DecodeReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 9> bytes{
        0x25, CMD_REPORT, VERSION_V2, ROLE_ALWAYS_ON_SLAVE, NODE_TYPE_ZWAVE_PLUS, 0x07, 0x00, 0x07, 0x01};
    EXPECT_FALSE(ZWavePlusInfo::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(ZWavePlusInfo, DecodeReportRejectsWrongCommand)
{
    // GET is not a Report — refuse so an inbound GET doesn't turn
    // into a phantom identification update.
    const std::array<std::uint8_t, 9> bytes{
        CC_ZWAVEPLUS_INFO, CMD_GET, VERSION_V2, ROLE_ALWAYS_ON_SLAVE, NODE_TYPE_ZWAVE_PLUS, 0x07, 0x00, 0x07, 0x01};
    EXPECT_FALSE(ZWavePlusInfo::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}
