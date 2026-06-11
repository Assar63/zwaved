#include "TransportService.hpp"

#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// Build a deterministic datagram of `n` bytes (0, 1, 2, ... wrapping).
auto makeDatagram(std::size_t n) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> data(n);
    std::iota(data.begin(), data.end(), std::uint8_t{0});
    return data;
}
}  // namespace

TEST(TransportService, FcsKnownVector)
{
    // CRC-16-CCITT seeded 0x1D0F (AUG-CCITT) over "123456789" is 0xE5CC —
    // the same algorithm CRC-16 Encapsulation uses, so it can be cross-checked.
    const std::vector<std::uint8_t> data{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(TransportService::fcs(std::span<const std::uint8_t>(data)), 0xE5CC);
}

TEST(TransportService, SplitSingleSegment)
{
    const auto datagram = makeDatagram(10);
    const auto segments = TransportService::splitOutbound(std::span<const std::uint8_t>(datagram), 3);
    ASSERT_EQ(segments.size(), 1U);
    const auto& seg = segments.front();
    // CC + props1 + size2 + props2 + 10 payload + 2 FCS.
    ASSERT_EQ(seg.size(), 4U + 10U + 2U);
    EXPECT_EQ(seg[0], TransportService::COMMAND_CLASS);
    EXPECT_EQ(seg[1] & TransportService::COMMAND_MASK, TransportService::TRANSPORT_SERVICE_FIRST_SEGMENT);
    EXPECT_EQ(seg[2], 10U);  // datagram size low byte
    EXPECT_EQ((seg[3] & TransportService::SESSION_ID_MASK) >> TransportService::SESSION_ID_SHIFT, 3U);
}

TEST(TransportService, SplitMultiSegment)
{
    const auto datagram = makeDatagram(250);
    const auto segments = TransportService::splitOutbound(std::span<const std::uint8_t>(datagram), 1);
    // 250 / 39 = 6 full segments + 1 remainder.
    ASSERT_EQ(segments.size(), 7U);
    EXPECT_EQ(segments.front()[1] & TransportService::COMMAND_MASK, TransportService::TRANSPORT_SERVICE_FIRST_SEGMENT);
    for (std::size_t i = 1; i < segments.size(); ++i)
    {
        EXPECT_EQ(segments[i][1] & TransportService::COMMAND_MASK,
                  TransportService::TRANSPORT_SERVICE_SUBSEQUENT_SEGMENT);
    }
}

TEST(TransportService, RoundTripInOrder)
{
    const auto datagram = makeDatagram(250);
    const auto segments = TransportService::splitOutbound(std::span<const std::uint8_t>(datagram), 5);

    TransportService::Assembler assembler;
    std::vector<std::uint8_t> result;
    for (const auto& seg : segments)
    {
        const auto done = assembler.feedSegment(std::span<const std::uint8_t>(seg));
        if (done.has_value())
        {
            result = *done;
        }
    }
    EXPECT_EQ(result, datagram);
}

TEST(TransportService, RoundTripOutOfOrder)
{
    const auto datagram = makeDatagram(250);
    auto segments       = TransportService::splitOutbound(std::span<const std::uint8_t>(datagram), 7);

    // Feed the FIRST segment, then the subsequent segments in reverse order.
    TransportService::Assembler assembler;
    std::vector<std::uint8_t> result;
    const auto feed = [&](const std::vector<std::uint8_t>& seg)
    {
        const auto done = assembler.feedSegment(std::span<const std::uint8_t>(seg));
        if (done.has_value())
        {
            result = *done;
        }
    };
    feed(segments.front());
    for (std::size_t i = segments.size(); i-- > 1;)
    {
        feed(segments[i]);
    }
    EXPECT_EQ(result, datagram);
}

TEST(TransportService, RejectsBadFcs)
{
    const auto datagram = makeDatagram(10);
    auto segments       = TransportService::splitOutbound(std::span<const std::uint8_t>(datagram), 2);
    ASSERT_EQ(segments.size(), 1U);
    segments.front().back() ^= 0xFF;  // corrupt the FCS

    TransportService::Assembler assembler;
    EXPECT_FALSE(assembler.feedSegment(std::span<const std::uint8_t>(segments.front())).has_value());
}

TEST(TransportService, DuplicateSegmentsAreIdempotent)
{
    const auto datagram = makeDatagram(120);
    const auto segments = TransportService::splitOutbound(std::span<const std::uint8_t>(datagram), 4);

    TransportService::Assembler assembler;
    std::vector<std::uint8_t> result;
    for (const auto& seg : segments)
    {
        // Feed every segment twice; the duplicate must not double-count.
        const auto first  = assembler.feedSegment(std::span<const std::uint8_t>(seg));
        const auto second = assembler.feedSegment(std::span<const std::uint8_t>(seg));
        if (first.has_value())
        {
            result = *first;
        }
        if (second.has_value())
        {
            result = *second;
        }
    }
    EXPECT_EQ(result, datagram);
}
