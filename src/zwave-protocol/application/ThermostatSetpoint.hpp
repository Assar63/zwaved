#ifndef ZWAVED_THERMOSTAT_SETPOINT_HPP
#define ZWAVED_THERMOSTAT_SETPOINT_HPP

// IWYU pragma: begin_exports
#include "ThermostatSetpoint.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Thermostat Setpoint Command Class (0x43). Sets/reads a target
/// temperature, one per setpoint type (heating, cooling, …).
///
/// SET and REPORT carry a `setpointType` byte (low 4 bits) followed by the
/// SDS13781 precision/scale/size encoded value — the exact encoding Sensor
/// Multilevel uses, shared here via EncodedValue. scale 0 = °C, 1 = °F.
///
/// Second of the split thermostat quartet (epic #23). SET/GET/REPORT only;
/// SUPPORTED_GET/REPORT and CAPABILITIES_GET are not implemented.
///
/// Command-byte constants come from ThermostatSetpoint.gen.hpp.
namespace ThermostatSetpoint
{
struct Report
{
    std::uint8_t setpointType = 0;  // 1 heating, 2 cooling, …
    std::uint8_t scale        = 0;  // 0 = °C, 1 = °F
    std::uint8_t precision    = 0;  // reading = value / 10^precision
    std::int32_t value        = 0;  // raw signed
};

/// Encode a Setpoint Set: `setpointType` (low 4 bits) + the
/// precision/scale/size encoded value.
[[nodiscard]] auto encodeSet(std::uint8_t setpointType,
                             std::uint8_t precision,
                             std::uint8_t scale,
                             std::int32_t value) -> std::vector<std::uint8_t>;

/// Encode a Setpoint Get for `setpointType` (low 4 bits).
[[nodiscard]] auto encodeGet(std::uint8_t setpointType) -> std::vector<std::uint8_t>;

/// Decode a Setpoint Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report
/// (wrong CC/cmd, invalid size field, or truncated value).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace ThermostatSetpoint

#endif  // ZWAVED_THERMOSTAT_SETPOINT_HPP
