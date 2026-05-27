#include "NodeVersion.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeGet body lives in NodeVersion.gen.cpp.

namespace
{
// Byte offsets of the Report fields inside the payload (after CC + cmd).
constexpr std::size_t OFFSET_LIBRARY_TYPE            = 2;
constexpr std::size_t OFFSET_PROTOCOL_VERSION        = 3;
constexpr std::size_t OFFSET_PROTOCOL_SUB_VERSION    = 4;
constexpr std::size_t OFFSET_APPLICATION_VERSION     = 5;
constexpr std::size_t OFFSET_APPLICATION_SUB_VERSION = 6;

// CC + cmd + libraryType + protocol(2) + application(2).
constexpr std::size_t REPORT_BYTES = 7;
}  // namespace

auto NodeVersion::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_BYTES || payload[0] != COMMAND_CLASS || payload[1] != VERSION_REPORT)
    {
        return std::nullopt;
    }
    Report out;
    out.libraryType           = payload[OFFSET_LIBRARY_TYPE];
    out.protocolVersion       = payload[OFFSET_PROTOCOL_VERSION];
    out.protocolSubVersion    = payload[OFFSET_PROTOCOL_SUB_VERSION];
    out.applicationVersion    = payload[OFFSET_APPLICATION_VERSION];
    out.applicationSubVersion = payload[OFFSET_APPLICATION_SUB_VERSION];
    return out;
}
