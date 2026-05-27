#include "ZWavePlusInfo.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeGet body lives in ZWavePlusInfo.gen.cpp.

namespace
{
constexpr unsigned BITS_PER_BYTE = 8;

// Byte offsets of the Report fields inside the payload (after CC + cmd).
constexpr std::size_t OFFSET_VERSION             = 2;
constexpr std::size_t OFFSET_ROLE_TYPE           = 3;
constexpr std::size_t OFFSET_NODE_TYPE           = 4;
constexpr std::size_t OFFSET_INSTALLER_ICON_TYPE = 5;
constexpr std::size_t OFFSET_USER_ICON_TYPE      = 7;

// CC + cmd + version + roleType + nodeType + installerIcon (2) + userIcon (2).
constexpr std::size_t REPORT_BYTES = 9;

auto readBeU16(std::span<const std::uint8_t> payload, std::size_t offset) -> std::uint16_t
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(payload[offset]) << BITS_PER_BYTE) |
                                      static_cast<std::uint16_t>(payload[offset + 1]));
}
}  // namespace

auto ZWavePlusInfo::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_BYTES || payload[0] != COMMAND_CLASS || payload[1] != ZWAVEPLUS_INFO_REPORT)
    {
        return std::nullopt;
    }
    Report out;
    out.zwavePlusVersion  = payload[OFFSET_VERSION];
    out.roleType          = payload[OFFSET_ROLE_TYPE];
    out.nodeType          = payload[OFFSET_NODE_TYPE];
    out.installerIconType = readBeU16(payload, OFFSET_INSTALLER_ICON_TYPE);
    out.userIconType      = readBeU16(payload, OFFSET_USER_ICON_TYPE);
    return out;
}
