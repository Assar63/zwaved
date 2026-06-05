#ifndef ZWAVED_COLOR_SWITCH_HPP
#define ZWAVED_COLOR_SWITCH_HPP

// IWYU pragma: begin_exports
#include "ColorSwitch.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Color Switch Command Class (0x33). Multi-component colour control
/// for RGB / RGBW / CCT lighting. Each component (red, green, blue, white,
/// …) carries its own 0..255 level.
///
/// GET asks for one `componentId`; REPORT carries that component's value
/// (v3+ adds the transition target + duration). SET carries a
/// variable-length list of `(componentId, value)` pairs plus a duration, so
/// one frame updates a whole colour atomically. Constants come from
/// ColorSwitch.gen.hpp.
///
/// SET/GET/REPORT only; SUPPORTED_GET/REPORT and START/STOP_LEVEL_CHANGE are
/// not implemented.
namespace ColorSwitch
{
struct Report
{
    std::uint8_t componentId = 0;
    std::uint8_t value       = 0;
    std::uint8_t targetValue = 0;  // mirrors value on v1 reports
    std::uint8_t duration    = 0;
};

/// Encode a Color Switch Get for one component.
[[nodiscard]] auto encodeGet(std::uint8_t componentId) -> std::vector<std::uint8_t>;

/// Encode a Color Switch Set from a flat `(componentId, value)` pair list
/// (`components` = {id0, val0, id1, val1, …}) plus a transition `duration`.
/// A trailing odd byte (incomplete pair) is ignored.
[[nodiscard]] auto encodeSet(std::span<const std::uint8_t> components,
                             std::uint8_t duration) -> std::vector<std::uint8_t>;

/// Decode a Color Switch Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report (wrong
/// CC/cmd or too short). v1 reports (no target/duration) yield
/// `targetValue == value` and `duration == 0`.
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace ColorSwitch

#endif  // ZWAVED_COLOR_SWITCH_HPP
