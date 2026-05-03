#ifndef ZWAVED_BASIC_HPP
#define ZWAVED_BASIC_HPP

// IWYU pragma: begin_exports
#include "Basic.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Basic Command Class (0x20). The "universal fallback" CC —
/// devices that don't implement a more specific CC for their primary
/// behaviour are required to map Basic SET / Basic GET / Basic REPORT
/// onto whatever does make sense (a binary switch responds On/Off, a
/// dimmer responds 0..99, a sensor reports its measured value).
/// Single-byte value semantics:
///   0x00       = off / inactive / level 0
///   0x01..0x63 = level 1..99 (dimmers, blinds, fan speed)
///   0x63       = max level (99 — used in place of 100 by spec)
///   0xFE       = unknown (Report-only)
///   0xFF       = on / full / "previous level" for dimmers
///
/// Constants and the simple encoders (encodeSet / encodeGet) are
/// generated from InterfaceManifest.yml and live in Basic.gen.hpp.
/// The hand-written part defines the decoded Report shape and the
/// v1/v2 wire-form decoder.
namespace Basic
{
/// Decoded Basic Report payload. v1 reports carry only `currentValue`
/// in the wire payload; v2+ adds a `targetValue` and a duration byte
/// (the time it'll take to transition from current to target). v1
/// reports leave `targetValue` and `duration` at their defaults — a
/// caller that needs to distinguish "stable" from "transitioning"
/// should compare `currentValue == targetValue` only when
/// `wireFormatV2` is true.
struct Report
{
    uint8_t currentValue = VALUE_UNKNOWN;
    uint8_t targetValue  = VALUE_UNKNOWN;
    uint8_t duration     = 0;
    bool wireFormatV2    = false;
};

/// Decode a Basic Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Accepts both v1 (3 bytes) and v2+ (5 bytes) wire forms. Returns
/// std::nullopt if the bytes are not a Basic Report.
[[nodiscard]] auto decodeReport(std::span<const uint8_t> payload) -> std::optional<Report>;
}  // namespace Basic

#endif  // ZWAVED_BASIC_HPP
