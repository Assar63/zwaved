#ifndef ZWAVED_MULTILEVEL_SWITCH_HPP
#define ZWAVED_MULTILEVEL_SWITCH_HPP

// IWYU pragma: begin_exports
#include "MultilevelSwitch.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Multilevel Switch Command Class (0x26). Dimmers, blinds,
/// fan speed. SET takes a value byte plus (v2+) a duration byte; the
/// daemon always emits the v2 wire form. Value-byte semantics:
///   0x00       = off / level 0
///   0x01..0x63 = level 1..99 (dimmer range)
///   0xFE       = unknown (Report-only)
///   0xFF       = on / "restore last level" (Set-only)
///
/// Constants and the simple encoders (encodeSet / encodeGet) are
/// generated from InterfaceManifest.yml and live in
/// MultilevelSwitch.gen.hpp. The hand-written part defines the
/// decoded Report shape and the v1/v2 wire-form decoder.
namespace MultilevelSwitch
{
/// Decoded Multilevel Switch Report payload. v1 reports carry only
/// `currentValue`; v2+ adds `targetValue` and `duration` (the time
/// it'll take to transition from current to target). v1 reports
/// mirror `currentValue` into `targetValue` and leave `duration` at
/// zero; `wireFormatV2 = false` lets a caller distinguish "stable v1
/// report" from "v2 report that happens to have current == target".
struct Report
{
    uint8_t currentValue = VALUE_UNKNOWN;
    uint8_t targetValue  = VALUE_UNKNOWN;
    uint8_t duration     = 0;
    bool wireFormatV2    = false;
};

/// Decode a Multilevel Switch Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Accepts both v1 (3 bytes) and v2+ (5 bytes) wire forms. Returns
/// std::nullopt if the bytes are not a Multilevel Switch Report.
[[nodiscard]] auto decodeReport(std::span<const uint8_t> payload) -> std::optional<Report>;
}  // namespace MultilevelSwitch

#endif  // ZWAVED_MULTILEVEL_SWITCH_HPP
