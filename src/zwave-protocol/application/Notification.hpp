#ifndef ZWAVED_NOTIFICATION_HPP
#define ZWAVED_NOTIFICATION_HPP

// IWYU pragma: begin_exports
#include "Notification.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Notification Command Class (0x71). Push events from sensors —
/// motion, water leak, smoke, tamper, etc. A v3+ Report carries legacy
/// v1 alarm fields (ignored here), a notificationStatus byte, a
/// `(notificationType, event)` pair, and a variable-length event-
/// parameter blob. The `(type, event)` matrix is large, so this decoder
/// returns the raw triple and leaves interpretation to callers.
///
/// Only the v3 type-filtered Get and the v3+ Report are handled; v1 alarm
/// Get/Report, Set, and SUPPORTED_GET/REPORT are not implemented.
///
/// Command-byte constants come from Notification.gen.hpp.
namespace Notification
{
struct Report
{
    std::uint8_t notificationType = 0;
    std::uint8_t event            = 0;
    std::uint8_t status           = 0;  // notificationStatus: 0x00 or 0xFF
    std::vector<std::uint8_t> parameters;
};

/// Encode a v3 Notification Get for `notificationType` (v1 alarmType and
/// the event filter are sent as 0 — "any pending of this type").
[[nodiscard]] auto encodeGet(std::uint8_t notificationType) -> std::vector<std::uint8_t>;

/// Decode a v3+ Notification Report payload (bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report
/// (wrong CC/cmd or truncated relative to the declared parameter length).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace Notification

#endif  // ZWAVED_NOTIFICATION_HPP
