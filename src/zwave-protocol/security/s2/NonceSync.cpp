#include "NonceSync.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
constexpr std::size_t HEADER_SIZE       = 2;  // CC + command
constexpr std::size_t REPORT_FIXED_SIZE = 4;  // CC + command + seq + flags
constexpr std::size_t EI_SIZE           = S2::SPAN::EI_SIZE;
constexpr std::size_t EXT_MIN_SIZE      = 2;  // length byte + flags/type byte
}  // namespace

auto S2::NonceSync::encodeNonceGet(std::uint8_t sequenceNumber) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, NONCE_GET, sequenceNumber};
}

auto S2::NonceSync::encodeNonceReport(const Report& report) -> std::vector<std::uint8_t>
{
    const auto flags = static_cast<std::uint8_t>((report.sos ? FLAG_SOS : 0) | (report.mos ? FLAG_MOS : 0));
    std::vector<std::uint8_t> out{COMMAND_CLASS, NONCE_REPORT, report.sequenceNumber, flags};
    if (report.sos && report.rei.has_value())
    {
        out.insert(out.end(), report.rei->begin(), report.rei->end());
    }
    return out;
}

auto S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_FIXED_SIZE || payload[0] != COMMAND_CLASS || payload[1] != NONCE_REPORT)
    {
        return std::nullopt;
    }
    Report report{.sequenceNumber = payload[2],
                  .sos            = (payload[3] & FLAG_SOS) != 0,
                  .mos            = (payload[3] & FLAG_MOS) != 0,
                  .rei            = std::nullopt};
    if (report.sos)
    {
        if (payload.size() < REPORT_FIXED_SIZE + EI_SIZE)
        {
            return std::nullopt;  // SOS promised a REI that isn't there
        }
        EntropyInput rei{};
        std::copy_n(payload.begin() + REPORT_FIXED_SIZE, EI_SIZE, rei.begin());
        report.rei = rei;
    }
    return report;
}

auto S2::NonceSync::encodeSpanExtension(const EntropyInput& senderEntropyInput) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{SPAN_EXTENSION_LENGTH, SPAN_EXTENSION_FLAGS};
    out.insert(out.end(), senderEntropyInput.begin(), senderEntropyInput.end());
    return out;
}

auto S2::NonceSync::findSpanExtension(std::span<const std::uint8_t> extensions) -> std::optional<EntropyInput>
{
    std::size_t offset = 0;
    while (offset + EXT_MIN_SIZE <= extensions.size())
    {
        const std::size_t length = extensions[offset];
        if (length < EXT_MIN_SIZE || offset + length > extensions.size())
        {
            return std::nullopt;  // malformed extension chain
        }
        const std::uint8_t flags = extensions[offset + 1];
        const bool moreToFollow  = (flags & EXT_MORE_FOLLOW_BIT) != 0;
        if ((flags & EXT_TYPE_MASK) == SPAN_EXTENSION_TYPE && length == SPAN_EXTENSION_LENGTH)
        {
            EntropyInput sei{};
            std::copy_n(extensions.begin() + static_cast<std::ptrdiff_t>(offset + EXT_MIN_SIZE), EI_SIZE, sei.begin());
            return sei;
        }
        if (!moreToFollow)
        {
            break;
        }
        offset += length;
    }
    return std::nullopt;
}

auto S2::NonceSync::commandByte(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>
{
    if (payload.size() < HEADER_SIZE || payload[0] != COMMAND_CLASS)
    {
        return std::nullopt;
    }
    return payload[1];
}
