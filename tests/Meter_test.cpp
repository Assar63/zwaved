#include "Meter.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// Build a Meter Report payload. `properties1` packs meterType (low 5 bits)
// + rateType (bits 5-6); `flags` packs precision<<5 | scale<<3 | size.
// `valueBytes` is the current value; `deltaTime` is the 16-bit BE field;
// `prevBytes` (appended only when non-empty) is the previous value.
auto report(std::uint8_t properties1,
            std::uint8_t flags,
            std::initializer_list<std::uint8_t> valueBytes,
            std::uint16_t deltaTime,
            std::initializer_list<std::uint8_t> prevBytes) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{Meter::COMMAND_CLASS, Meter::METER_REPORT, properties1, flags};
    out.insert(out.end(), valueBytes);
    out.push_back(static_cast<std::uint8_t>(deltaTime >> 8));
    out.push_back(static_cast<std::uint8_t>(deltaTime & 0xFF));
    out.insert(out.end(), prevBytes);
    return out;
}
}  // namespace

TEST(Meter, EncodeGetCarriesScale)
{
    // scale 2 → bits 3-4 = 0b10000 = 0x10.
    const std::vector<std::uint8_t> expected{Meter::COMMAND_CLASS, Meter::METER_GET, 0x10};
    EXPECT_EQ(Meter::encodeGet(2), expected);
}

TEST(Meter, ElectricKwhWithPreviousValue)
{
    // meterType=1 (electric), rateType=1 (import) → properties1 = 0x21.
    // flags: precision=3, scale=0, size=2 → (3<<5)|(0<<3)|2 = 0x62.
    // value=12345 (0x3039) → 12.345 kWh; deltaTime=3600; prev=12000 (0x2EE0).
    const auto decoded = Meter::decodeReport(report(0x21, 0x62, {0x30, 0x39}, 3600, {0x2E, 0xE0}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->meterType, Meter::TYPE_ELECTRIC);
    EXPECT_EQ(decoded->rateType, 1);
    EXPECT_EQ(decoded->scale, 0);
    EXPECT_EQ(decoded->precision, 3);
    EXPECT_EQ(decoded->value, 12345);
    EXPECT_EQ(decoded->deltaTime, 3600);
    EXPECT_TRUE(decoded->hasPrevious);
    EXPECT_EQ(decoded->previousValue, 12000);
}

TEST(Meter, WaterInstantaneousNoPrevious)
{
    // meterType=3 (water) → properties1 = 0x03. flags: precision=0, scale=0,
    // size=4 → 0x04. value=1000000 (0x000F4240); deltaTime=0 → no previous.
    const auto decoded = Meter::decodeReport(report(0x03, 0x04, {0x00, 0x0F, 0x42, 0x40}, 0, {}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->meterType, Meter::TYPE_WATER);
    EXPECT_EQ(decoded->value, 1000000);
    EXPECT_EQ(decoded->deltaTime, 0);
    EXPECT_FALSE(decoded->hasPrevious);
    EXPECT_EQ(decoded->previousValue, 0);
}

TEST(Meter, NegativeValueSignExtends)
{
    // electric power (W), size=2, value=-50 (0xFFCE), no previous.
    const auto decoded = Meter::decodeReport(report(0x21, 0x12, {0xFF, 0xCE}, 0, {}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, -50);
}

TEST(Meter, RejectsInvalidSize)
{
    // size field 3 is invalid (only 1/2/4 allowed).
    EXPECT_FALSE(Meter::decodeReport(report(0x21, 0x03, {0x00, 0x01, 0x02}, 0, {})).has_value());
}

TEST(Meter, RejectsTruncatedAndMalformed)
{
    // deltaTime != 0 but the promised previous value is missing.
    const std::vector<std::uint8_t> truncatedPrev{
        Meter::COMMAND_CLASS, Meter::METER_REPORT, 0x21, 0x62, 0x30, 0x39, 0x0E, 0x10};  // deltaTime=3600, no prev
    EXPECT_FALSE(Meter::decodeReport(truncatedPrev).has_value());

    // Shorter than the 4-byte header.
    const std::vector<std::uint8_t> tooShort{Meter::COMMAND_CLASS, Meter::METER_REPORT, 0x21};
    EXPECT_FALSE(Meter::decodeReport(tooShort).has_value());

    // Wrong command class.
    const std::vector<std::uint8_t> wrongCc{0x80, Meter::METER_REPORT, 0x21, 0x12, 0x00, 0x05, 0x00, 0x00};
    EXPECT_FALSE(Meter::decodeReport(wrongCc).has_value());

    // Wrong command byte (Get, not Report).
    const std::vector<std::uint8_t> wrongCmd{
        Meter::COMMAND_CLASS, Meter::METER_GET, 0x21, 0x12, 0x00, 0x05, 0x00, 0x00};
    EXPECT_FALSE(Meter::decodeReport(wrongCmd).has_value());
}
