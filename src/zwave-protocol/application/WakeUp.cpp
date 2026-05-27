#include "WakeUp.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// encodeGet and encodeNoMoreInformation bodies live in WakeUp.gen.cpp.

namespace
{
constexpr unsigned BITS_PER_BYTE = 8;
constexpr unsigned BYTE_MASK     = 0xFFU;

// CC + cmd + seconds (3 bytes BE) + controllerNodeId.
constexpr std::size_t INTERVAL_REPORT_BYTES = 6;
constexpr std::size_t OFFSET_SECONDS_MSB    = 2;
constexpr std::size_t OFFSET_SECONDS_MID    = 3;
constexpr std::size_t OFFSET_SECONDS_LSB    = 4;
constexpr std::size_t OFFSET_CONTROLLER     = 5;

// CC + cmd.
constexpr std::size_t NOTIFICATION_BYTES = 2;
}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire shape is fixed by WAKE_UP_INTERVAL_SET
auto WakeUp::encodeIntervalSet(std::uint32_t seconds, std::uint8_t controllerNodeId) -> std::vector<std::uint8_t>
{
    // Spec field is 24-bit unsigned. Clamp rather than truncate so
    // a caller passing a too-large seconds doesn't silently end up
    // setting an interval ~16M shorter than intended.
    const std::uint32_t clamped = std::min<std::uint32_t>(seconds, INTERVAL_MAX);
    return {
        COMMAND_CLASS,
        WAKE_UP_SET,
        static_cast<std::uint8_t>((clamped >> (2U * BITS_PER_BYTE)) & BYTE_MASK),
        static_cast<std::uint8_t>((clamped >> BITS_PER_BYTE) & BYTE_MASK),
        static_cast<std::uint8_t>(clamped & BYTE_MASK),
        controllerNodeId,
    };
}

auto WakeUp::decodeIntervalReport(std::span<const std::uint8_t> payload) -> std::optional<IntervalReport>
{
    if (payload.size() < INTERVAL_REPORT_BYTES || payload[0] != COMMAND_CLASS || payload[1] != WAKE_UP_REPORT)
    {
        return std::nullopt;
    }
    IntervalReport out;
    out.seconds = (static_cast<std::uint32_t>(payload[OFFSET_SECONDS_MSB]) << (2U * BITS_PER_BYTE)) |
                  (static_cast<std::uint32_t>(payload[OFFSET_SECONDS_MID]) << BITS_PER_BYTE) |
                  static_cast<std::uint32_t>(payload[OFFSET_SECONDS_LSB]);
    out.controllerNodeId = payload[OFFSET_CONTROLLER];
    return out;
}

auto WakeUp::decodeNotification(std::span<const std::uint8_t> payload) -> std::optional<Notification>
{
    if (payload.size() < NOTIFICATION_BYTES || payload[0] != COMMAND_CLASS || payload[1] != WAKE_UP_NOTIFICATION)
    {
        return std::nullopt;
    }
    return Notification{};
}
