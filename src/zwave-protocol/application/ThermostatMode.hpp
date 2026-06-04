#ifndef ZWAVED_THERMOSTAT_MODE_HPP
#define ZWAVED_THERMOSTAT_MODE_HPP

// IWYU pragma: begin_exports
#include "ThermostatMode.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Thermostat Mode Command Class (0x40). Sets/reads the HVAC
/// operating mode (off / heat / cool / auto / fan only / dry / …).
///
/// SET and REPORT both carry a single mode byte whose low 5 bits are the
/// mode; the high 3 bits are a v3 manufacturer-data-field count, which
/// this decoder masks off. `encodeSet(mode)` / `encodeGet()` come from
/// ThermostatMode.gen.hpp.
///
/// First of the split thermostat quartet (epic #23). SET/GET/REPORT only;
/// SUPPORTED_GET/REPORT are not implemented.
namespace ThermostatMode
{
struct Report
{
    std::uint8_t mode = 0;  // 0 off, 1 heat, 2 cool, 3 auto, …
};

/// Decode a Thermostat Mode Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report
/// (wrong CC/cmd or too short).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace ThermostatMode

#endif  // ZWAVED_THERMOSTAT_MODE_HPP
