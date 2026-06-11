#include "TransportService.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint16_t FCS_INIT  = 0x1D0F;
constexpr std::uint16_t FCS_POLY  = 0x1021;
constexpr int BITS_PER_BYTE       = 8;
constexpr std::uint16_t MSB_MASK  = 0x8000;
constexpr std::uint16_t BYTE_MASK = 0x00FF;
constexpr std::size_t FCS_LEN     = 2;
}  // namespace

auto TransportService::fcs(std::span<const std::uint8_t> data) -> std::uint16_t
{
    std::uint16_t crc = FCS_INIT;
    for (const auto byte : data)
    {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(byte) << BITS_PER_BYTE));
        for (int bit = 0; bit < BITS_PER_BYTE; ++bit)
        {
            crc = (crc & MSB_MASK) != 0 ? static_cast<std::uint16_t>((crc << 1) ^ FCS_POLY)
                                        : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

namespace
{
// Append the big-endian FCS computed over everything in `seg` so far.
auto appendFcs(std::vector<std::uint8_t>& seg) -> void
{
    const auto crc = TransportService::fcs(std::span<const std::uint8_t>(seg));
    seg.push_back(static_cast<std::uint8_t>((crc >> BITS_PER_BYTE) & BYTE_MASK));
    seg.push_back(static_cast<std::uint8_t>(crc & BYTE_MASK));
}
}  // namespace

auto TransportService::splitOutbound(std::span<const std::uint8_t> datagram,
                                     std::uint8_t sessionId) -> std::vector<std::vector<std::uint8_t>>
{
    std::vector<std::vector<std::uint8_t>> out;
    const std::size_t size = datagram.size();
    if (size == 0)
    {
        return out;
    }
    const auto sizeHigh = static_cast<std::uint8_t>((size >> BITS_PER_BYTE) & DATAGRAM_SIZE_MASK);
    const auto sizeLow  = static_cast<std::uint8_t>(size & BYTE_MASK);
    const auto session  = static_cast<std::uint8_t>((sessionId << SESSION_ID_SHIFT) & SESSION_ID_MASK);

    std::size_t offset = 0;
    bool first         = true;
    while (offset < size)
    {
        const std::size_t chunk = std::min(SEGMENT_PAYLOAD_MAX, size - offset);
        std::vector<std::uint8_t> seg;
        seg.push_back(COMMAND_CLASS);
        if (first)
        {
            seg.push_back(static_cast<std::uint8_t>(TRANSPORT_SERVICE_FIRST_SEGMENT | sizeHigh));
            seg.push_back(sizeLow);
            seg.push_back(session);  // Ext = 0
        }
        else
        {
            seg.push_back(static_cast<std::uint8_t>(TRANSPORT_SERVICE_SUBSEQUENT_SEGMENT | sizeHigh));
            seg.push_back(sizeLow);
            seg.push_back(static_cast<std::uint8_t>(session | ((offset >> BITS_PER_BYTE) & OFFSET_HIGH_MASK)));
            seg.push_back(static_cast<std::uint8_t>(offset & BYTE_MASK));
        }
        const auto payload = datagram.subspan(offset, chunk);
        seg.insert(seg.end(), payload.begin(), payload.end());
        appendFcs(seg);
        out.push_back(std::move(seg));
        offset += chunk;
        first = false;
    }
    return out;
}

auto TransportService::Assembler::reset() -> void
{
    active_        = false;
    receivedCount_ = 0;
    buffer_.clear();
    received_.clear();
}

auto TransportService::Assembler::feedSegment(std::span<const std::uint8_t> segment)
    -> std::optional<std::vector<std::uint8_t>>
{
    if (segment.size() < FCS_LEN + 1 || segment[0] != COMMAND_CLASS)
    {
        return std::nullopt;
    }
    const std::size_t bodyLen = segment.size() - FCS_LEN;
    const auto expected       = fcs(segment.subspan(0, bodyLen));
    const auto actual = static_cast<std::uint16_t>((static_cast<std::uint16_t>(segment[bodyLen]) << BITS_PER_BYTE) |
                                                   (segment[bodyLen + 1] & BYTE_MASK));
    if (expected != actual)
    {
        return std::nullopt;  // corrupt segment — drop (SEGMENT_REQUEST deferred)
    }

    const auto command = static_cast<std::uint8_t>(segment[1] & COMMAND_MASK);
    if (command == TRANSPORT_SERVICE_FIRST_SEGMENT)
    {
        return handleFirst(segment, bodyLen);
    }
    if (command == TRANSPORT_SERVICE_SUBSEQUENT_SEGMENT)
    {
        return handleSubsequent(segment, bodyLen);
    }
    return std::nullopt;  // SEGMENT_COMPLETE / REQUEST / WAIT — not handled in the MVP
}

auto TransportService::Assembler::payloadStart(std::span<const std::uint8_t> segment,
                                               std::size_t bodyLen,
                                               std::size_t baseHeader) -> std::optional<std::size_t>
{
    std::size_t start = baseHeader;
    if ((segment[3] & EXT_FLAG) != 0)
    {
        if (bodyLen < baseHeader + 1)
        {
            return std::nullopt;
        }
        start = baseHeader + 1 + segment[baseHeader];  // skip header-extension length + bytes
    }
    if (start > bodyLen)
    {
        return std::nullopt;
    }
    return start;
}

auto TransportService::Assembler::handleFirst(std::span<const std::uint8_t> segment,
                                              std::size_t bodyLen) -> std::optional<std::vector<std::uint8_t>>
{
    constexpr std::size_t header = 4;  // CC + props1 + size2 + props2
    if (bodyLen < header)
    {
        return std::nullopt;
    }
    datagramSize_    = (static_cast<std::size_t>(segment[1] & DATAGRAM_SIZE_MASK) << BITS_PER_BYTE) | segment[2];
    sessionId_       = static_cast<std::uint8_t>((segment[3] & SESSION_ID_MASK) >> SESSION_ID_SHIFT);
    const auto start = payloadStart(segment, bodyLen, header);
    if (datagramSize_ == 0 || !start.has_value())
    {
        return std::nullopt;
    }
    buffer_.assign(datagramSize_, 0);
    received_.assign(datagramSize_, false);
    receivedCount_ = 0;
    active_        = true;
    return place(0, segment.subspan(*start, bodyLen - *start));
}

auto TransportService::Assembler::handleSubsequent(std::span<const std::uint8_t> segment,
                                                   std::size_t bodyLen) -> std::optional<std::vector<std::uint8_t>>
{
    constexpr std::size_t header = 5;  // CC + props1 + size2 + props2 + offset2
    if (!active_ || bodyLen < header)
    {
        return std::nullopt;
    }
    const auto session = static_cast<std::uint8_t>((segment[3] & SESSION_ID_MASK) >> SESSION_ID_SHIFT);
    if (session != sessionId_)
    {
        return std::nullopt;  // a different session — ignore (MVP: single session)
    }
    const std::size_t offset = (static_cast<std::size_t>(segment[3] & OFFSET_HIGH_MASK) << BITS_PER_BYTE) | segment[4];
    const auto start         = payloadStart(segment, bodyLen, header);
    if (!start.has_value())
    {
        return std::nullopt;
    }
    return place(offset, segment.subspan(*start, bodyLen - *start));
}

auto TransportService::Assembler::place(std::size_t offset, std::span<const std::uint8_t> payload)
    -> std::optional<std::vector<std::uint8_t>>
{
    for (std::size_t i = 0; i < payload.size(); ++i)
    {
        const std::size_t pos = offset + i;
        if (pos >= datagramSize_)
        {
            break;  // segment claims to extend past the datagram — ignore the overrun
        }
        if (!received_[pos])
        {
            buffer_[pos]   = payload[i];
            received_[pos] = true;
            ++receivedCount_;
        }
    }
    if (receivedCount_ == datagramSize_)
    {
        active_                        = false;
        std::vector<std::uint8_t> done = std::move(buffer_);
        buffer_.clear();
        received_.clear();
        receivedCount_ = 0;
        return done;
    }
    return std::nullopt;
}
