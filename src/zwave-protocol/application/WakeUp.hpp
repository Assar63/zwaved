#ifndef ZWAVED_WAKE_UP_HPP
#define ZWAVED_WAKE_UP_HPP

// IWYU pragma: begin_exports
#include "WakeUp.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Wake Up Command Class (0x84). Sleeping-node lifecycle.
///
/// Interval Set / Get / Report carry a 24-bit big-endian seconds
/// field plus a single-byte controllerNodeId (the node the sleeper
/// will phone home to). Notification is a node → host signal that
/// the node is briefly awake to receive pending commands;
/// NoMoreInformation is the daemon's "go back to sleep now" reply.
///
/// encodeIntervalGet and encodeNoMoreInformation are empty-payload
/// frames and come from WakeUp.gen.cpp. encodeIntervalSet is
/// hand-written because the 3-byte BE u24 field can't be expressed
/// in the manifest's simple-encoder payload syntax today.
namespace WakeUp
{
constexpr std::uint32_t INTERVAL_MAX = 0x00FFFFFFU;  // 24-bit wire field

/// Decoded Wake Up Interval Report payload.
struct IntervalReport
{
    std::uint32_t seconds         = 0;
    std::uint8_t controllerNodeId = 0;
};

/// Wake Up Notification has no payload beyond the (CC, cmd) pair —
/// the frame itself is the signal. Empty struct kept for decoder
/// signature consistency with the other CCs.
struct Notification
{
};

/// Encode WAKE_UP_INTERVAL_SET. `seconds` is clamped to 24 bits
/// (INTERVAL_MAX) so the caller can't accidentally truncate a
/// larger value silently.
[[nodiscard]] auto encodeIntervalSet(std::uint32_t seconds, std::uint8_t controllerNodeId) -> std::vector<std::uint8_t>;

/// Decode a Wake Up Interval Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a Wake Up Interval
/// Report.
[[nodiscard]] auto decodeIntervalReport(std::span<const std::uint8_t> payload) -> std::optional<IntervalReport>;

/// Decode a Wake Up Notification frame. Returns an empty
/// `Notification` when the bytes match `(0x84, 0x07)`,
/// std::nullopt otherwise.
[[nodiscard]] auto decodeNotification(std::span<const std::uint8_t> payload) -> std::optional<Notification>;
}  // namespace WakeUp

#endif  // ZWAVED_WAKE_UP_HPP
