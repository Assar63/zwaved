#ifndef ZWAVED_INDICATOR_HPP
#define ZWAVED_INDICATOR_HPP

// IWYU pragma: begin_exports
#include "Indicator.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Indicator Command Class (0x87). Controls a node's indicator
/// (LED / buzzer). v1 carries a single value byte: 0x00 off, 0x01..0x63
/// level, 0xFF on. SET / GET / REPORT. The v3+ structured form
/// (indicatorId + property/value tuples for multi-indicator devices) is a
/// follow-up. Constants and the SET / GET encoders are generated into
/// Indicator.gen.hpp; the hand-written part is the Report decode.
namespace Indicator
{
/// Decoded Indicator Report (v1) — the node's current indicator value.
struct Report
{
    std::uint8_t value = 0;
};

/// Decode an Indicator Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not an Indicator Report.
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace Indicator

#endif  // ZWAVED_INDICATOR_HPP
