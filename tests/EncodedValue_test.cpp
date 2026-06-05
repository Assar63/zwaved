#include "EncodedValue.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
auto bytes(std::initializer_list<std::uint8_t> list) -> std::vector<std::uint8_t>
{
    return list;
}
}  // namespace

TEST(EncodedValue, DecodeTwoByteCelsius)
{
    // precision=1, scale=0, size=2 → flags 0x22; value 215 (0x00D7) = 21.5.
    const auto decoded = EncodedValue::decode(0x22, bytes({0x00, 0xD7}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->size, 2);
    EXPECT_EQ(decoded->scale, 0);
    EXPECT_EQ(decoded->precision, 1);
    EXPECT_EQ(decoded->value, 215);
}

TEST(EncodedValue, DecodeFahrenheitScaleBit)
{
    // scale=1 (°F), precision=1, size=2 → flags 0x2A; value 706 = 70.6 °F.
    const auto decoded = EncodedValue::decode(0x2A, bytes({0x02, 0xC2}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->scale, 1);
    EXPECT_EQ(decoded->value, 706);
}

TEST(EncodedValue, DecodeNegativeSignExtends)
{
    // precision=1, size=2, value -15 (0xFFF1).
    const auto decoded = EncodedValue::decode(0x22, bytes({0xFF, 0xF1}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, -15);
}

TEST(EncodedValue, DecodeSingleByteSigned)
{
    // size=1, value -1 (0xFF).
    const auto decoded = EncodedValue::decode(0x01, bytes({0xFF}));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->value, -1);
}

TEST(EncodedValue, DecodeRejectsInvalidSizeAndTruncation)
{
    // size field 3 is invalid.
    EXPECT_FALSE(EncodedValue::decode(0x03, bytes({0x00, 0x01, 0x02})).has_value());
    // size 4 declared but only 2 value bytes present.
    EXPECT_FALSE(EncodedValue::decode(0x04, bytes({0x00, 0x01})).has_value());
}

TEST(EncodedValue, EncodePicksSmallestSize)
{
    // value 100 fits in 1 byte; precision=0, scale=0 → flags 0x01.
    EXPECT_EQ(EncodedValue::encode(0, 0, 100), bytes({0x01, 0x64}));
    // value 300 needs 2 bytes (0x012C); flags 0x02.
    EXPECT_EQ(EncodedValue::encode(0, 0, 300), bytes({0x02, 0x01, 0x2C}));
    // value 100000 needs 4 bytes (0x000186A0); flags 0x04.
    EXPECT_EQ(EncodedValue::encode(0, 0, 100000), bytes({0x04, 0x00, 0x01, 0x86, 0xA0}));
}

TEST(EncodedValue, EncodePacksPrecisionAndScale)
{
    // precision=1, scale=1, value 215 (1 byte fits? 215 > 127 → 2 bytes).
    // flags = (1<<5)|(1<<3)|2 = 0x2A; value 0x00D7.
    EXPECT_EQ(EncodedValue::encode(1, 1, 215), bytes({0x2A, 0x00, 0xD7}));
}

TEST(EncodedValue, EncodeDecodeRoundTrip)
{
    for (const std::int32_t value : {-32768, -200, -1, 0, 1, 127, 128, 32767, 100000, -100000})
    {
        const auto frame   = EncodedValue::encode(2, 1, value);
        const auto decoded = EncodedValue::decode(frame.front(), std::span{frame}.subspan(1));
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->value, value);
        EXPECT_EQ(decoded->precision, 2);
        EXPECT_EQ(decoded->scale, 1);
    }
}
