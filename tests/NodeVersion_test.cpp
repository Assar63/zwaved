#include "NodeVersion.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_VERSION = 0x86;
constexpr std::uint8_t CMD_GET    = 0x11;
constexpr std::uint8_t CMD_REPORT = 0x12;

// HostApi::LIBRARY_TYPE_SLAVE — most application nodes report this.
constexpr std::uint8_t LIBRARY_TYPE_SLAVE = 4;
}  // namespace

TEST(NodeVersion, EncodeGet)
{
    // Version CC's commands live in the 0x10s (GET=0x11, REPORT=0x12)
    // rather than 0x01-0x05 like most CCs. Assert the encoded byte
    // explicitly so a manifest typo would fail loudly.
    const std::vector<std::uint8_t> expected{CC_VERSION, CMD_GET};
    EXPECT_EQ(NodeVersion::encodeGet(), expected);
}

TEST(NodeVersion, DecodeReportSlaveProtocol6_07App1_03)
{
    // Realistic Aeotec-class application node: Slave library, Z-Wave
    // protocol 6.07, application firmware 1.03.
    const std::array<std::uint8_t, 7> bytes{CC_VERSION, CMD_REPORT, LIBRARY_TYPE_SLAVE, 0x06, 0x07, 0x01, 0x03};
    const auto report = NodeVersion::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->libraryType, LIBRARY_TYPE_SLAVE);
    EXPECT_EQ(report->protocolVersion, 0x06);
    EXPECT_EQ(report->protocolSubVersion, 0x07);
    EXPECT_EQ(report->applicationVersion, 0x01);
    EXPECT_EQ(report->applicationSubVersion, 0x03);
}

TEST(NodeVersion, DecodeReportIgnoresV2TrailingBytes)
{
    // v2 Reports append hardwareVersion + per-firmware-target list.
    // The decoder treats them as opaque trailing bytes and returns
    // the same v1 Report shape.
    const std::array<std::uint8_t, 10> bytes{
        CC_VERSION, CMD_REPORT, LIBRARY_TYPE_SLAVE, 0x06, 0x07, 0x01, 0x03, 0x42, 0x00, 0x00};
    const auto report = NodeVersion::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->applicationVersion, 0x01);
    EXPECT_EQ(report->applicationSubVersion, 0x03);
}

TEST(NodeVersion, DecodeReportRejectsTooShort)
{
    // Six bytes: missing applicationSubVersion.
    const std::array<std::uint8_t, 6> bytes{CC_VERSION, CMD_REPORT, LIBRARY_TYPE_SLAVE, 0x06, 0x07, 0x01};
    EXPECT_FALSE(NodeVersion::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(NodeVersion, DecodeReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 7> bytes{0x25, CMD_REPORT, LIBRARY_TYPE_SLAVE, 0x06, 0x07, 0x01, 0x03};
    EXPECT_FALSE(NodeVersion::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(NodeVersion, DecodeReportRejectsWrongCommand)
{
    // GET is not a Report — refuse so an inbound GET doesn't get
    // misparsed as a phantom firmware-version update.
    const std::array<std::uint8_t, 7> bytes{CC_VERSION, CMD_GET, LIBRARY_TYPE_SLAVE, 0x06, 0x07, 0x01, 0x03};
    EXPECT_FALSE(NodeVersion::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}
