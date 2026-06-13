// Security S2 (CC 0x9F) KEX handshake codec + grant policy (#183).

#include "Kex.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
template <std::size_t N> auto span(const std::array<std::uint8_t, N>& arr) -> std::span<const std::uint8_t>
{
    return std::span<const std::uint8_t>(arr);
}
}  // namespace

TEST(S2Kex, EncodeGet)
{
    EXPECT_EQ(S2::Kex::encodeGet(), (std::vector<std::uint8_t>{0x9F, 0x04}));
}

TEST(S2Kex, ReportRoundTrip)
{
    const S2::Kex::Report report{.echo             = false,
                                 .requestCsa       = true,
                                 .supportedSchemes = S2::Kex::KEX_SCHEME_1,
                                 .supportedCurves  = S2::Kex::ECDH_CURVE25519,
                                 .requestedKeys    = S2::Kex::KEY_S2_AUTHENTICATED | S2::Kex::KEY_S2_UNAUTHENTICATED};
    const auto frame = S2::Kex::encodeReport(report);
    ASSERT_EQ(frame.size(), 6U);
    EXPECT_EQ(frame[0], 0x9F);
    EXPECT_EQ(frame[1], 0x05);
    const auto decoded = S2::Kex::decodeReport(std::span<const std::uint8_t>(frame));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, report);
}

TEST(S2Kex, SetRoundTrip)
{
    const S2::Kex::Set set{.echo           = false,
                           .requestCsa     = false,
                           .selectedScheme = S2::Kex::KEX_SCHEME_1,
                           .selectedCurve  = S2::Kex::ECDH_CURVE25519,
                           .grantedKeys    = S2::Kex::KEY_S2_UNAUTHENTICATED};
    const auto frame = S2::Kex::encodeSet(set);
    ASSERT_EQ(frame.size(), 6U);
    EXPECT_EQ(frame[1], 0x06);
    const auto decoded = S2::Kex::decodeSet(std::span<const std::uint8_t>(frame));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, set);
}

TEST(S2Kex, FailRoundTrip)
{
    EXPECT_EQ(S2::Kex::encodeFail(S2::Kex::FAIL_KEY), (std::vector<std::uint8_t>{0x9F, 0x07, 0x01}));
    const std::array<std::uint8_t, 3> frame{0x9F, 0x07, S2::Kex::FAIL_CANCEL};
    const auto decoded = S2::Kex::decodeFail(span(frame));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, S2::Kex::FAIL_CANCEL);
}

TEST(S2Kex, DecodeRejectsMalformed)
{
    const std::array<std::uint8_t, 6> wrongCmd{0x9F, 0x06, 0, 0, 0, 0};  // a SET, not a REPORT
    EXPECT_FALSE(S2::Kex::decodeReport(span(wrongCmd)).has_value());
    const std::array<std::uint8_t, 6> wrongCc{0x25, 0x05, 0, 0, 0, 0};
    EXPECT_FALSE(S2::Kex::decodeReport(span(wrongCc)).has_value());
    const std::array<std::uint8_t, 4> tooShort{0x9F, 0x05, 0, 0};
    EXPECT_FALSE(S2::Kex::decodeReport(span(tooShort)).has_value());
}

TEST(S2Kex, GrantKeysIntersectsRequestedAndSupported)
{
    const S2::Kex::Report report{.requestedKeys =
                                     static_cast<std::uint8_t>(S2::Kex::KEY_S2_UNAUTHENTICATED |
                                                               S2::Kex::KEY_S2_AUTHENTICATED | S2::Kex::KEY_S0)};
    // Controller supports everything except S0.
    const auto supported = static_cast<std::uint8_t>(S2::Kex::KEY_S2_UNAUTHENTICATED | S2::Kex::KEY_S2_AUTHENTICATED |
                                                     S2::Kex::KEY_S2_ACCESS_CONTROL);
    const auto granted   = S2::Kex::grantKeys(report, supported);
    EXPECT_EQ(granted, static_cast<std::uint8_t>(S2::Kex::KEY_S2_UNAUTHENTICATED | S2::Kex::KEY_S2_AUTHENTICATED));
}

TEST(S2Kex, GrantKeysDeniesAccessControlWhenCsaRequested)
{
    const S2::Kex::Report report{
        .requestCsa    = true,  // no DSK label → can't do DSK confirmation
        .requestedKeys = static_cast<std::uint8_t>(S2::Kex::KEY_S2_ACCESS_CONTROL | S2::Kex::KEY_S2_AUTHENTICATED)};
    const auto supported = static_cast<std::uint8_t>(S2::Kex::KEY_S2_ACCESS_CONTROL | S2::Kex::KEY_S2_AUTHENTICATED);
    const auto granted   = S2::Kex::grantKeys(report, supported);
    EXPECT_EQ(granted & S2::Kex::KEY_S2_ACCESS_CONTROL, 0);  // denied
    EXPECT_NE(granted & S2::Kex::KEY_S2_AUTHENTICATED, 0);   // still granted
}

TEST(S2Kex, CommandByte)
{
    const std::array<std::uint8_t, 2> get{0x9F, 0x04};
    const auto cmd = S2::Kex::commandByte(span(get));
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(*cmd, 0x04);
    const std::array<std::uint8_t, 2> notS2{0x25, 0x01};
    EXPECT_FALSE(S2::Kex::commandByte(span(notS2)).has_value());
}
