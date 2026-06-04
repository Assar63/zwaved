#ifndef ZWAVED_SENSOR_MULTILEVEL_HPP
#define ZWAVED_SENSOR_MULTILEVEL_HPP

// IWYU pragma: begin_exports
#include "SensorMultilevel.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Sensor Multilevel Command Class (0x31). Reports a sensor
/// reading — temperature, humidity, luminance, power, etc. The Report
/// payload is: `sensorType`, a packed flag byte
/// (`precision << 5 | scale << 3 | size`), then a signed big-endian
/// `value` of `size` (1/2/4) bytes. The real reading is
/// `value / 10^precision` in the unit selected by `(sensorType, scale)`
/// — e.g. air temperature (type 0x01) scale 0 = °C, 1 = °F.
///
/// Only the v1 Report path is decoded; the v5+ type-filtered Get and the
/// SUPPORTED_GET/REPORT discovery commands are not implemented.
///
/// Constants and `encodeGet()` come from SensorMultilevel.gen.hpp.
namespace SensorMultilevel
{
struct Report
{
    std::uint8_t sensorType = 0;
    std::uint8_t scale      = 0;
    std::uint8_t precision  = 0;
    std::int32_t value      = 0;  // raw; reading = value / 10^precision
};

/// Decode a Sensor Multilevel Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report
/// (wrong CC/cmd, invalid size field, or truncated value).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace SensorMultilevel

#endif  // ZWAVED_SENSOR_MULTILEVEL_HPP
