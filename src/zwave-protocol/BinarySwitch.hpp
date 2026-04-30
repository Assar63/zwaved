#ifndef ZWAVED_BINARY_SWITCH_HPP
#define ZWAVED_BINARY_SWITCH_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Binary Switch Command Class (0x25), version 1. See AWG
/// Z-Wave Specifications §2.2.20. Encodes the application-layer
/// payload that a controller sends inside FUNC_ID_ZW_SEND_DATA to
/// drive a node's On/Off state.
namespace BinarySwitch
{
constexpr uint8_t COMMAND_CLASS        = 0x25;
constexpr uint8_t SWITCH_BINARY_SET    = 0x01;
constexpr uint8_t SWITCH_BINARY_GET    = 0x02;
constexpr uint8_t SWITCH_BINARY_REPORT = 0x03;

constexpr uint8_t VALUE_OFF     = 0x00;
constexpr uint8_t VALUE_ON      = 0xFF;
constexpr uint8_t VALUE_UNKNOWN = 0xFE;  // Report-only, version 1

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

/// Build the CC payload bytes for SWITCH_BINARY_SET. The caller is
/// responsible for wrapping the result in FUNC_ID_ZW_SEND_DATA.
[[nodiscard]] auto encodeSet(bool turnOn) -> std::vector<uint8_t>;

/// Build the CC payload bytes for SWITCH_BINARY_GET.
[[nodiscard]] auto encodeGet() -> std::vector<uint8_t>;

/// Decode a SWITCH_BINARY_REPORT payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a Binary Switch Report.
[[nodiscard]] auto decodeReport(std::span<const uint8_t> payload) -> std::optional<Report>;
}  // namespace BinarySwitch

#endif  // ZWAVED_BINARY_SWITCH_HPP
