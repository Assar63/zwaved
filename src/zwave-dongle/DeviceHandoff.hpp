#ifndef ZWAVED_DEVICE_HANDOFF_HPP
#define ZWAVED_DEVICE_HANDOFF_HPP

#include <atomic>
#include <optional>
#include <string>

/**
 * Single-slot publication channel for the Z-Wave dongle's TTY path.
 * The MonitorThread publishes/clears as udev events arrive; the
 * ProtocolThread blocks in awaitDevicePath() until a path is available
 * or shutdown is requested.
 */
namespace DeviceHandoff
{
auto publish(const std::string& ttyPath) -> void;
auto clear() -> void;

/// Block until a TTY path is available or stopFlag becomes false.
/// Returns the current path on availability, std::nullopt on shutdown.
[[nodiscard]] auto awaitDevicePath(const std::atomic<bool>& stopFlag) -> std::optional<std::string>;

/// Notify all waiters without changing state. Used during shutdown to
/// release any thread blocked in awaitDevicePath().
auto wakeAll() -> void;

[[nodiscard]] auto current() -> std::string;
}  // namespace DeviceHandoff

#endif  // ZWAVED_DEVICE_HANDOFF_HPP
