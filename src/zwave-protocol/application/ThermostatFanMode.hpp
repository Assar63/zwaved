#ifndef ZWAVED_THERMOSTAT_FAN_MODE_HPP
#define ZWAVED_THERMOSTAT_FAN_MODE_HPP

// IWYU pragma: begin_exports
#include "ThermostatFanMode.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Thermostat Fan Mode Command Class (0x44). Sets/reads the HVAC
/// fan behaviour. SET and REPORT carry one properties byte: the low 4 bits
/// are the fan mode, bit 7 is a separate "fan off" flag.
///
/// `encodeSet(mode, off)` / `encodeGet()` come from
/// ThermostatFanMode.gen.hpp (the SET payload is a manifest expression, so
/// both encoders are generated). Only the byte-splitting `decodeReport` is
/// hand-written.
///
/// Last of the split thermostat quartet (epic #23). SET/GET/REPORT only;
/// SUPPORTED_GET/REPORT are not implemented.
namespace ThermostatFanMode
{
struct Report
{
    std::uint8_t mode = 0;      // 0 auto low, 1 low, 2 auto high, 3 high, …
    bool off          = false;  // bit 7: fan forced off
};

/// Decode a Thermostat Fan Mode Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report
/// (wrong CC/cmd or too short).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace ThermostatFanMode

#endif  // ZWAVED_THERMOSTAT_FAN_MODE_HPP
