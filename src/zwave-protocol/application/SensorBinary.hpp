#ifndef ZWAVED_SENSOR_BINARY_HPP
#define ZWAVED_SENSOR_BINARY_HPP

// IWYU pragma: begin_exports
#include "SensorBinary.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Sensor Binary Command Class (0x30). Reports a binary sensor
/// state — door/window contact, motion, tamper. Deprecated in favour of
/// Notification (CC 0x71) but ubiquitous on legacy devices.
///
/// A v1 Report is a single `value` byte (0x00 idle / 0xFF active). A v2
/// Report appends a `sensorType` byte for devices that pack several
/// binary sensors. This decoder accepts both; for a v1 frame `sensorType`
/// is reported as 0.
///
/// Only the Report path is decoded; the v2 type-filtered Get and the
/// SUPPORTED_GET/REPORT discovery commands are not implemented.
///
/// Constants and `encodeGet()` come from SensorBinary.gen.hpp.
namespace SensorBinary
{
struct Report
{
    std::uint8_t sensorType = 0;  // 0 for v1 reports; device code for v2+
    std::uint8_t value      = 0;  // 0x00 idle / 0xFF active
};

/// Decode a Sensor Binary Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report
/// (wrong CC/cmd or too short for even the v1 single-value form).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace SensorBinary

#endif  // ZWAVED_SENSOR_BINARY_HPP
