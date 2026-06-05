#ifndef ZWAVED_THERMOSTAT_OPERATING_STATE_HPP
#define ZWAVED_THERMOSTAT_OPERATING_STATE_HPP

// IWYU pragma: begin_exports
#include "ThermostatOperatingState.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Thermostat Operating State Command Class (0x42). Read-only report
/// of what the HVAC is currently doing (idle / heating / cooling / fan).
///
/// GET takes no payload; REPORT carries a state byte whose low 4 bits are
/// the state. `encodeGet()` comes from ThermostatOperatingState.gen.hpp.
///
/// Third of the split thermostat quartet (epic #23). GET/REPORT only — no
/// Set (read-only CC); SUPPORTED_GET/REPORT are not implemented.
namespace ThermostatOperatingState
{
struct Report
{
    std::uint8_t state = 0;  // 0 idle, 1 heating, 2 cooling, 3 fan only, …
};

/// Decode a Thermostat Operating State Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report
/// (wrong CC/cmd or too short).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace ThermostatOperatingState

#endif  // ZWAVED_THERMOSTAT_OPERATING_STATE_HPP
