#ifndef ZWAVED_BATTERY_HPP
#define ZWAVED_BATTERY_HPP

// IWYU pragma: begin_exports
#include "Battery.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Battery Command Class (0x80). One-shot query for the
/// battery percentage of a battery-powered node. The Report's `level`
/// byte is the wire value:
///   0x00..0x64 = 0..100 % charge
///   0xFF       = low-battery warning (no precise gauge)
/// `lowBattery` is the derived flag for callers that only care
/// whether the cell is below the spec threshold.
///
/// v2 adds charging / health bitfields after `level`; this decoder
/// ignores those trailing bytes and surfaces only the percentage.
///
/// Constants and `encodeGet()` come from Battery.gen.hpp.
namespace Battery
{
struct Report
{
    std::uint8_t level = 0;
    bool lowBattery    = false;
};

/// Decode a Battery Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a Battery Report.
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace Battery

#endif  // ZWAVED_BATTERY_HPP
