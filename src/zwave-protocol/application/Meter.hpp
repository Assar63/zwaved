#ifndef ZWAVED_METER_HPP
#define ZWAVED_METER_HPP

// IWYU pragma: begin_exports
#include "Meter.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Meter Command Class (0x32). Accumulated-consumption metering —
/// electric (kWh/W/V/A), gas, water. Common on smart plugs.
///
/// A v3+ Report carries `meterType` + `rateType` in the first properties
/// byte, a packed `precision`/`scale`/`size` flag byte, a signed
/// big-endian current `value` of 1/2/4 bytes, a 16-bit `deltaTime`
/// (seconds since the previous sample), and — only when `deltaTime != 0`
/// — a `previousValue` of the same width. The real reading is
/// `value / 10^precision` in the unit selected by `(meterType, scale)`.
///
/// Only the Report path and a v2+ Get (with a scale selector) are
/// implemented; v4 dual-scale, SUPPORTED_GET/REPORT, and RESET are not.
///
/// Constants come from Meter.gen.hpp.
namespace Meter
{
struct Report
{
    std::uint8_t meterType     = 0;  // 1 electric, 2 gas, 3 water
    std::uint8_t rateType      = 0;  // 1 import, 2 export
    std::uint8_t scale         = 0;  // unit selector, meterType-specific
    std::uint8_t precision     = 0;  // reading = value / 10^precision
    std::int32_t value         = 0;  // raw signed current reading
    std::uint16_t deltaTime    = 0;  // seconds since previousValue sample; 0 = none
    std::int32_t previousValue = 0;
    bool hasPrevious           = false;
};

/// Encode a v2+ Meter Get for the given `scale` (0..3). The scale rides
/// in bits 3-4 of the single payload byte; scale values needing the v4
/// dual-scale extension are not supported (masked to the low 2 bits).
[[nodiscard]] auto encodeGet(std::uint8_t scale) -> std::vector<std::uint8_t>;

/// Decode a Meter Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report
/// (wrong CC/cmd, invalid size field, or truncated relative to the
/// declared value / deltaTime / previousValue widths).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;

/// Human-readable meter-type name ("electric" / "gas" / "water"), or
/// nullptr for an unknown type.
[[nodiscard]] auto meterTypeName(std::uint8_t meterType) -> const char*;

/// Unit string for a `(meterType, scale)` pair (e.g. electric scale 0 =
/// "kWh", scale 2 = "W"). Returns "" when the pair is unknown.
[[nodiscard]] auto scaleUnit(std::uint8_t meterType, std::uint8_t scale) -> const char*;
}  // namespace Meter

#endif  // ZWAVED_METER_HPP
