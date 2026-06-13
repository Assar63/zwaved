// Security S2 (CC 0x9F) network-key install codec (#185).

#include "Kex.hpp"
#include "KeyInstall.hpp"

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
const S2::Crypto::Key KEY{
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
}  // namespace

TEST(S2KeyInstall, KeyGetRoundTrip)
{
    EXPECT_EQ(S2::KeyInstall::encodeKeyGet(S2::Kex::KEY_S2_ACCESS_CONTROL),
              (std::vector<std::uint8_t>{0x9F, 0x09, 0x04}));
    const std::array<std::uint8_t, 3> frame{0x9F, 0x09, S2::Kex::KEY_S2_AUTHENTICATED};
    const auto requested = S2::KeyInstall::decodeKeyGet(span(frame));
    ASSERT_TRUE(requested.has_value());
    EXPECT_EQ(*requested, S2::Kex::KEY_S2_AUTHENTICATED);
}

TEST(S2KeyInstall, KeyReportRoundTrip)
{
    const auto frame = S2::KeyInstall::encodeKeyReport(S2::Kex::KEY_S2_UNAUTHENTICATED, KEY);
    ASSERT_EQ(frame.size(), 3U + S2::Crypto::KEY_SIZE);
    EXPECT_EQ(frame[0], 0x9F);
    EXPECT_EQ(frame[1], 0x0A);
    EXPECT_EQ(frame[2], S2::Kex::KEY_S2_UNAUTHENTICATED);
    const auto report = S2::KeyInstall::decodeKeyReport(std::span<const std::uint8_t>(frame));
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->grantedKey, S2::Kex::KEY_S2_UNAUTHENTICATED);
    EXPECT_EQ(report->key, KEY);
}

TEST(S2KeyInstall, KeyVerifyEncode)
{
    EXPECT_EQ(S2::KeyInstall::encodeKeyVerify(), (std::vector<std::uint8_t>{0x9F, 0x0B}));
}

TEST(S2KeyInstall, TransferEndRoundTrip)
{
    const auto verified = S2::KeyInstall::encodeTransferEnd(true, false);
    ASSERT_EQ(verified.size(), 3U);
    EXPECT_EQ(verified[2] & S2::KeyInstall::TRANSFER_KEY_VERIFIED, S2::KeyInstall::TRANSFER_KEY_VERIFIED);
    const auto decodedVerified = S2::KeyInstall::decodeTransferEnd(std::span<const std::uint8_t>(verified));
    ASSERT_TRUE(decodedVerified.has_value());
    EXPECT_TRUE(decodedVerified->keyVerified);
    EXPECT_FALSE(decodedVerified->keyRequestComplete);

    const auto complete        = S2::KeyInstall::encodeTransferEnd(false, true);
    const auto decodedComplete = S2::KeyInstall::decodeTransferEnd(std::span<const std::uint8_t>(complete));
    ASSERT_TRUE(decodedComplete.has_value());
    EXPECT_FALSE(decodedComplete->keyVerified);
    EXPECT_TRUE(decodedComplete->keyRequestComplete);
}

TEST(S2KeyInstall, ClassForKeyBit)
{
    EXPECT_EQ(S2::KeyInstall::classForKeyBit(S2::Kex::KEY_S2_UNAUTHENTICATED),
              std::optional{S2::NetworkKeys::Class::Unauthenticated});
    EXPECT_EQ(S2::KeyInstall::classForKeyBit(S2::Kex::KEY_S2_AUTHENTICATED),
              std::optional{S2::NetworkKeys::Class::Authenticated});
    EXPECT_EQ(S2::KeyInstall::classForKeyBit(S2::Kex::KEY_S2_ACCESS_CONTROL),
              std::optional{S2::NetworkKeys::Class::AccessControl});
    EXPECT_EQ(S2::KeyInstall::classForKeyBit(S2::Kex::KEY_S0), std::optional{S2::NetworkKeys::Class::S0Compat});
    EXPECT_FALSE(S2::KeyInstall::classForKeyBit(0x40).has_value());  // reserved bit
    EXPECT_FALSE(S2::KeyInstall::classForKeyBit(0x03).has_value());  // multiple bits
}

TEST(S2KeyInstall, DecodeRejectsMalformed)
{
    const std::array<std::uint8_t, 3> wrongCc{0x25, 0x09, 0x01};
    EXPECT_FALSE(S2::KeyInstall::decodeKeyGet(span(wrongCc)).has_value());
    const std::array<std::uint8_t, 2> tooShort{0x9F, 0x09};
    EXPECT_FALSE(S2::KeyInstall::decodeKeyGet(span(tooShort)).has_value());
    const std::array<std::uint8_t, 5> shortReport{0x9F, 0x0A, 0x01, 0x00, 0x00};
    EXPECT_FALSE(S2::KeyInstall::decodeKeyReport(span(shortReport)).has_value());
}

TEST(S2KeyInstall, CommandByte)
{
    const std::array<std::uint8_t, 2> verify{0x9F, 0x0B};
    const auto cmd = S2::KeyInstall::commandByte(span(verify));
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(*cmd, 0x0B);
}
