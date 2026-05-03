#ifndef ZWAVED_BINARY_SWITCH_HPP
#define ZWAVED_BINARY_SWITCH_HPP

// IWYU pragma: begin_exports
#include "BinarySwitch.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Binary Switch Command Class (0x25), version 1. See AWG
/// Z-Wave Specifications §2.2.20. Encodes the application-layer
/// payload that a controller sends inside FUNC_ID_ZW_SEND_DATA to
/// drive a node's On/Off state.
///
/// Constants and the simple encoders (encodeSet / encodeGet) are
/// generated from InterfaceManifest.yml and live in
/// BinarySwitch.gen.hpp. The hand-written part defines the decoded
/// Report shape and the State enum the decoder synthesizes from the
/// raw value byte.
namespace BinarySwitch
{
enum class State : uint8_t
{
    Off     = 0,
    On      = 1,
    Unknown = 2,
};

struct Report
{
    State state      = State::Unknown;
    uint8_t rawValue = VALUE_UNKNOWN;
};

/// Decode a SWITCH_BINARY_REPORT payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a Binary Switch Report.
[[nodiscard]] auto decodeReport(std::span<const uint8_t> payload) -> std::optional<Report>;
}  // namespace BinarySwitch

#endif  // ZWAVED_BINARY_SWITCH_HPP
