// Security S2 (CC 0x9F) nonce-sync codec (#187/#199): NONCE_GET / NONCE_REPORT
// + the SPAN Extension. Pins the SDS13783 §4.2.6.5.9-10 + §4.2.6.5.13 wire
// shapes and the round-trips.

#include "NonceSync.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const S2::NonceSync::EntropyInput REI{
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF};
const S2::NonceSync::EntropyInput SEI{
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F};
}  // namespace

TEST(S2NonceSync, NonceGetWireShape)
{
    EXPECT_EQ(S2::NonceSync::encodeNonceGet(0x07), (std::vector<std::uint8_t>{0x9F, 0x01, 0x07}));
}

TEST(S2NonceSync, NonceReportSosRoundTrip)
{
    const S2::NonceSync::Report report{.sequenceNumber = 0x42, .sos = true, .mos = false, .rei = REI};
    const auto encoded = S2::NonceSync::encodeNonceReport(report);

    // [CC][cmd][seq][flags=SOS] + 16-byte REI
    ASSERT_EQ(encoded.size(), 4U + 16U);
    EXPECT_EQ(encoded[0], 0x9F);
    EXPECT_EQ(encoded[1], 0x02);
    EXPECT_EQ(encoded[2], 0x42);
    EXPECT_EQ(encoded[3], S2::NonceSync::FLAG_SOS);

    const auto decoded = S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(encoded));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, report);
}

TEST(S2NonceSync, NonceReportMosWithoutReiHasNoEntropy)
{
    const S2::NonceSync::Report report{.sequenceNumber = 1, .sos = false, .mos = true, .rei = std::nullopt};
    const auto encoded = S2::NonceSync::encodeNonceReport(report);
    ASSERT_EQ(encoded.size(), 4U);  // no REI when SOS is clear
    EXPECT_EQ(encoded[3], S2::NonceSync::FLAG_MOS);

    const auto decoded = S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(encoded));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded->sos);
    EXPECT_TRUE(decoded->mos);
    EXPECT_FALSE(decoded->rei.has_value());
}

TEST(S2NonceSync, DecodeRejectsTruncatedSosReport)
{
    // SOS flag set but only 8 of the 16 REI bytes present.
    std::vector<std::uint8_t> truncated{0x9F, 0x02, 0x00, S2::NonceSync::FLAG_SOS, 1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_FALSE(S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(truncated)).has_value());
}

TEST(S2NonceSync, DecodeRejectsWrongCommand)
{
    std::vector<std::uint8_t> notAReport{0x9F, 0x01, 0x00, 0x00};  // NONCE_GET, not REPORT
    EXPECT_FALSE(S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(notAReport)).has_value());
}

TEST(S2NonceSync, SpanExtensionWireShapeAndRoundTrip)
{
    const auto ext = S2::NonceSync::encodeSpanExtension(SEI);

    ASSERT_EQ(ext.size(), 18U);
    EXPECT_EQ(ext[0], 18);    // length includes itself
    EXPECT_EQ(ext[1], 0x41);  // moreToFollow=0, critical=1, type=SPAN(1)

    const auto found = S2::NonceSync::findSpanExtension(std::span<const std::uint8_t>(ext));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, SEI);
}

TEST(S2NonceSync, FindSpanExtensionSkipsAnEarlierExtension)
{
    // A 4-byte unknown extension (type 0x05, more-to-follow set) then the SPAN one.
    std::vector<std::uint8_t> chain{0x04, static_cast<std::uint8_t>(0x80 | 0x05), 0xDE, 0xAD};
    const auto span = S2::NonceSync::encodeSpanExtension(SEI);
    chain.insert(chain.end(), span.begin(), span.end());

    const auto found = S2::NonceSync::findSpanExtension(std::span<const std::uint8_t>(chain));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, SEI);
}

TEST(S2NonceSync, FindSpanExtensionRejectsMalformedLength)
{
    std::vector<std::uint8_t> bad{0x01, 0x41};  // length 1 < the 2-byte minimum
    EXPECT_FALSE(S2::NonceSync::findSpanExtension(std::span<const std::uint8_t>(bad)).has_value());
    EXPECT_FALSE(S2::NonceSync::findSpanExtension({}).has_value());
}
