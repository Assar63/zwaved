#ifndef ZWAVED_BASIC_HPP
#define ZWAVED_BASIC_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Basic Command Class (0x20). The "universal fallback" CC —
/// devices that don't implement a more specific CC for their primary
/// behaviour are required to map Basic SET / Basic GET / Basic REPORT
/// onto whatever does make sense (a binary switch responds On/Off, a
/// dimmer responds 0..99, a sensor reports its measured value).
/// Single-byte value semantics:
///   0x00       = off / inactive / level 0
///   0x01..0x63 = level 1..99 (dimmers, blinds, fan speed)
///   0x63       = max level (99 — used in place of 100 by spec)
///   0xFE       = unknown (Report-only)
///   0xFF       = on / full / "previous level" for dimmers
namespace Basic
{
constexpr uint8_t COMMAND_CLASS = 0x20;
constexpr uint8_t BASIC_SET     = 0x01;
constexpr uint8_t BASIC_GET     = 0x02;
constexpr uint8_t BASIC_REPORT  = 0x03;

constexpr uint8_t VALUE_OFF     = 0x00;
constexpr uint8_t VALUE_ON      = 0xFF;
constexpr uint8_t VALUE_UNKNOWN = 0xFE;

/// Decoded Basic Report payload. v1 reports carry only `currentValue`
/// in the wire payload; v2+ adds a `targetValue` and a duration byte
/// (the time it'll take to transition from current to target). v1
/// reports leave `targetValue` and `duration` at their defaults — a
/// caller that needs to distinguish "stable" from "transitioning"
/// should compare `currentValue == targetValue` only when
/// `wireFormatV2` is true.
struct Report
{
    uint8_t currentValue = VALUE_UNKNOWN;
    uint8_t targetValue  = VALUE_UNKNOWN;
    uint8_t duration     = 0;
    bool wireFormatV2    = false;
};

/// Build the CC payload for Basic SET. Caller decides the value byte;
/// 0x00 is off, 0xFF is on / full, 0x01..0x63 is a dimmer level. The
/// caller is responsible for wrapping the result in
/// FUNC_ID_ZW_SEND_DATA.
[[nodiscard]] auto encodeSet(uint8_t value) -> std::vector<uint8_t>;

/// Build the CC payload for Basic GET.
[[nodiscard]] auto encodeGet() -> std::vector<uint8_t>;

/// Decode a Basic Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Accepts both v1 (3 bytes) and v2+ (5 bytes) wire forms. Returns
/// std::nullopt if the bytes are not a Basic Report.
[[nodiscard]] auto decodeReport(std::span<const uint8_t> payload) -> std::optional<Report>;
}  // namespace Basic

#endif  // ZWAVED_BASIC_HPP
