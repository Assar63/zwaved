#ifndef ZWAVED_ZWAVEPLUS_INFO_HPP
#define ZWAVED_ZWAVEPLUS_INFO_HPP

// IWYU pragma: begin_exports
#include "ZWavePlusInfo.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Plus Info Command Class (0x5E). Identifies how a Z-Wave
/// Plus node positions itself in the network — protocol version,
/// role / node type, and a pair of device-database icon hints. The
/// two icon fields are u16 values (big-endian on the wire) assigned
/// by the Z-Wave alliance.
///
/// Unusually for a Z-Wave CC, GET is byte 0x01 and REPORT is
/// byte 0x02 (most CCs use 0x02/0x03 or 0x04/0x05) — follow the
/// spec, not the pattern. Constants come from ZWavePlusInfo.gen.hpp.
namespace ZWavePlusInfo
{
struct Report
{
    std::uint8_t zwavePlusVersion   = 0;
    std::uint8_t roleType           = 0;
    std::uint8_t nodeType           = 0;
    std::uint16_t installerIconType = 0;
    std::uint16_t userIconType      = 0;
};

/// Decode a Z-Wave Plus Info Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a Z-Wave Plus Info
/// Report.
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace ZWavePlusInfo

#endif  // ZWAVED_ZWAVEPLUS_INFO_HPP
