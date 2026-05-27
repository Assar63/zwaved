#include "ManufacturerSpecific.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_MANUFACTURER_SPECIFIC = 0x72;
constexpr std::uint8_t CMD_GET                  = 0x04;
constexpr std::uint8_t CMD_REPORT               = 0x05;

// Aeotec's assigned manufacturer ID (0x0086) + their Z-Stick Gen5
// product-type (0x0001) + product-id (0x005A). Realistic shape for
// a Report a controller might see in the wild.
constexpr std::uint16_t AEOTEC_MANUFACTURER_ID = 0x0086;
constexpr std::uint16_t AEOTEC_PRODUCT_TYPE_ID = 0x0001;
constexpr std::uint16_t AEOTEC_PRODUCT_ID      = 0x005A;
}  // namespace

TEST(ManufacturerSpecific, EncodeGet)
{
    const std::vector<std::uint8_t> expected{CC_MANUFACTURER_SPECIFIC, CMD_GET};
    EXPECT_EQ(ManufacturerSpecific::encodeGet(), expected);
}

TEST(ManufacturerSpecific, DecodeReportAeotec)
{
    // Big-endian u16s on the wire: MSB first, then LSB.
    const std::array<std::uint8_t, 8> bytes{CC_MANUFACTURER_SPECIFIC, CMD_REPORT, 0x00, 0x86, 0x00, 0x01, 0x00, 0x5A};
    const auto report = ManufacturerSpecific::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->manufacturerId, AEOTEC_MANUFACTURER_ID);
    EXPECT_EQ(report->productTypeId, AEOTEC_PRODUCT_TYPE_ID);
    EXPECT_EQ(report->productId, AEOTEC_PRODUCT_ID);
}

TEST(ManufacturerSpecific, DecodeReportMaxIds)
{
    // 0xFFFF in every slot exercises the high byte / low byte split.
    const std::array<std::uint8_t, 8> bytes{CC_MANUFACTURER_SPECIFIC, CMD_REPORT, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const auto report = ManufacturerSpecific::decodeReport(std::span<const std::uint8_t>(bytes));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->manufacturerId, 0xFFFFU);
    EXPECT_EQ(report->productTypeId, 0xFFFFU);
    EXPECT_EQ(report->productId, 0xFFFFU);
}

TEST(ManufacturerSpecific, DecodeReportRejectsTooShort)
{
    // Seven bytes: enough for CC + CMD + two u16s, missing the last
    // byte of productId.
    const std::array<std::uint8_t, 7> bytes{CC_MANUFACTURER_SPECIFIC, CMD_REPORT, 0x00, 0x86, 0x00, 0x01, 0x00};
    EXPECT_FALSE(ManufacturerSpecific::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(ManufacturerSpecific, DecodeReportRejectsWrongCommandClass)
{
    const std::array<std::uint8_t, 8> bytes{0x25, CMD_REPORT, 0x00, 0x86, 0x00, 0x01, 0x00, 0x5A};
    EXPECT_FALSE(ManufacturerSpecific::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}

TEST(ManufacturerSpecific, DecodeReportRejectsWrongCommand)
{
    // GET is not a Report — refuse so an inbound GET doesn't turn
    // into a phantom identification update with bogus IDs read from
    // wherever the bytes happen to be.
    const std::array<std::uint8_t, 8> bytes{CC_MANUFACTURER_SPECIFIC, CMD_GET, 0x00, 0x86, 0x00, 0x01, 0x00, 0x5A};
    EXPECT_FALSE(ManufacturerSpecific::decodeReport(std::span<const std::uint8_t>(bytes)).has_value());
}
