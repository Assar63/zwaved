#include "Configuration.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_CONFIGURATION = 0x70;
constexpr std::uint8_t CMD_SET          = 0x04;
constexpr std::uint8_t CMD_GET          = 0x05;
constexpr std::uint8_t CMD_REPORT       = 0x06;

// Realistic Fibaro-style parameter: param 7 = auto-shutoff timer
// in seconds, 2 bytes unsigned.
constexpr std::uint8_t PARAMETER_AUTO_SHUTOFF = 7;
}  // namespace

TEST(Configuration, EncodeSet1Byte)
{
    // value = 42 in 1 byte → 0x2A on the wire.
    const std::vector<std::uint8_t> expected{CC_CONFIGURATION, CMD_SET, PARAMETER_AUTO_SHUTOFF, 1, 0x2A};
    EXPECT_EQ(Configuration::encodeSet(PARAMETER_AUTO_SHUTOFF, 1, 42), expected);
}

TEST(Configuration, EncodeSet2BytePositive)
{
    // 300 seconds = 0x012C — exercises BE high/low.
    const std::vector<std::uint8_t> expected{CC_CONFIGURATION, CMD_SET, PARAMETER_AUTO_SHUTOFF, 2, 0x01, 0x2C};
    EXPECT_EQ(Configuration::encodeSet(PARAMETER_AUTO_SHUTOFF, 2, 300), expected);
}

TEST(Configuration, EncodeSet4BytePositive)
{
    // 1_000_000 = 0x000F4240.
    const std::vector<std::uint8_t> expected{
        CC_CONFIGURATION, CMD_SET, PARAMETER_AUTO_SHUTOFF, 4, 0x00, 0x0F, 0x42, 0x40};
    EXPECT_EQ(Configuration::encodeSet(PARAMETER_AUTO_SHUTOFF, 4, 1'000'000), expected);
}

TEST(Configuration, EncodeSet4ByteNegative)
{
    // -1 as int32 = 0xFFFFFFFF — every byte must be 0xFF.
    const std::vector<std::uint8_t> expected{
        CC_CONFIGURATION, CMD_SET, PARAMETER_AUTO_SHUTOFF, 4, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(Configuration::encodeSet(PARAMETER_AUTO_SHUTOFF, 4, -1), expected);
}

TEST(Configuration, EncodeSet1ByteNegativeTruncatesToTwosComplement)
{
    // -1 in 1 byte = 0xFF — the encoder takes the low byte of the
    // int32 two's-complement form, so a -1 / 0xFF / 255 produce
    // identical wire bytes (because they're identical low bytes).
    const std::vector<std::uint8_t> expected{CC_CONFIGURATION, CMD_SET, PARAMETER_AUTO_SHUTOFF, 1, 0xFF};
    EXPECT_EQ(Configuration::encodeSet(PARAMETER_AUTO_SHUTOFF, 1, -1), expected);
}

TEST(Configuration, EncodeSetRejectsInvalidSize)
{
    // Spec only permits 1, 2, or 4. The encoder returns an empty
    // vector rather than producing a malformed frame; ProtocolThread
    // handlers should validate `size` before calling.
    EXPECT_TRUE(Configuration::encodeSet(PARAMETER_AUTO_SHUTOFF, 3, 0).empty());
    EXPECT_TRUE(Configuration::encodeSet(PARAMETER_AUTO_SHUTOFF, 0, 0).empty());
    EXPECT_TRUE(Configuration::encodeSet(PARAMETER_AUTO_SHUTOFF, 8, 0).empty());
}

TEST(Configuration, EncodeGet)
{
    const std::vector<std::uint8_t> expected{CC_CONFIGURATION, CMD_GET, PARAMETER_AUTO_SHUTOFF};
    EXPECT_EQ(Configuration::encodeGet(PARAMETER_AUTO_SHUTOFF), expected);
}

TEST(Configuration, DecodeReport1Byte)
{
    const std::array<std::uint8_t, 5> bytes{CC_CONFIGURATION, CMD_REPORT, PARAMETER_AUTO_SHUTOFF, 1, 0x2A};
    const auto report = Configuration::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->parameter, PARAMETER_AUTO_SHUTOFF);
    EXPECT_EQ(report->size, 1);
    EXPECT_EQ(report->value, 42);
}

TEST(Configuration, DecodeReport2Byte)
{
    const std::array<std::uint8_t, 6> bytes{CC_CONFIGURATION, CMD_REPORT, PARAMETER_AUTO_SHUTOFF, 2, 0x01, 0x2C};
    const auto report = Configuration::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->size, 2);
    EXPECT_EQ(report->value, 300);
}

TEST(Configuration, DecodeReport4ByteSignedNegative)
{
    // 0xFFFFFFFF on the wire is -1 sign-extended. Verifies the
    // sign-extension path for size=4 (the value already IS the
    // int32, just reinterpreted).
    const std::array<std::uint8_t, 8> bytes{
        CC_CONFIGURATION, CMD_REPORT, PARAMETER_AUTO_SHUTOFF, 4, 0xFF, 0xFF, 0xFF, 0xFF};
    const auto report = Configuration::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->size, 4);
    EXPECT_EQ(report->value, -1);
}

TEST(Configuration, DecodeReport1ByteSignedNegative)
{
    // 0xFF in size=1 = -1 after sign-extension. Caller who knows
    // their parameter is unsigned can `static_cast<std::uint32_t>`
    // back to 0xFFFFFFFF / 255.
    const std::array<std::uint8_t, 5> bytes{CC_CONFIGURATION, CMD_REPORT, PARAMETER_AUTO_SHUTOFF, 1, 0xFF};
    const auto report = Configuration::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->value, -1);
}

TEST(Configuration, DecodeReport2ByteSignedNegative)
{
    // 0xFFFE in size=2 = -2 after sign-extension.
    const std::array<std::uint8_t, 6> bytes{CC_CONFIGURATION, CMD_REPORT, PARAMETER_AUTO_SHUTOFF, 2, 0xFF, 0xFE};
    const auto report = Configuration::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->value, -2);
}

TEST(Configuration, DecodeReportRejectsInvalidSize)
{
    // size = 3 is not in {1, 2, 4} per spec. Refuse.
    const std::array<std::uint8_t, 7> bytes{CC_CONFIGURATION, CMD_REPORT, PARAMETER_AUTO_SHUTOFF, 3, 0x00, 0x00, 0x00};
    EXPECT_FALSE(Configuration::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Configuration, DecodeReportRejectsTruncatedValue)
{
    // Says size=4 but only 2 value bytes present.
    const std::array<std::uint8_t, 6> bytes{CC_CONFIGURATION, CMD_REPORT, PARAMETER_AUTO_SHUTOFF, 4, 0x00, 0x0F};
    EXPECT_FALSE(Configuration::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Configuration, DecodeReportRejectsTooShortHeader)
{
    // Missing the size byte entirely.
    const std::array<std::uint8_t, 3> bytes{CC_CONFIGURATION, CMD_REPORT, PARAMETER_AUTO_SHUTOFF};
    EXPECT_FALSE(Configuration::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Configuration, DecodeReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 5> bytes{0x25, CMD_REPORT, PARAMETER_AUTO_SHUTOFF, 1, 0x2A};
    EXPECT_FALSE(Configuration::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(Configuration, DecodeReportRejectsWrongCommand)
{
    // GET is not a Report. Refuse so an inbound GET doesn't turn
    // into a phantom Report.
    const std::array<std::uint8_t, 5> bytes{CC_CONFIGURATION, CMD_GET, PARAMETER_AUTO_SHUTOFF, 1, 0x2A};
    EXPECT_FALSE(Configuration::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}
