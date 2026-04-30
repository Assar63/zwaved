#ifndef ZWAVED_HOST_API_CALLBACK_DISPATCHER_HPP
#define ZWAVED_HOST_API_CALLBACK_DISPATCHER_HPP

#include "HostApi.hpp"

#include <atomic>
#include <optional>

namespace HostApi
{
/// Push a decoded callback to consumers (the external API thread).
auto publishCallback(const NodeStatusCallback& callback) -> void;

/// Block waiting for a callback, up to timeoutMs. Returns std::nullopt
/// if the wait expires or stopFlag becomes false.
[[nodiscard]] auto popCallback(const std::atomic<bool>& stopFlag, int timeoutMs) -> std::optional<NodeStatusCallback>;

/// Wake any blocked consumer. Used during shutdown.
auto wakeAllCallbacks() -> void;

/// Clear any pending callbacks; used after a serial reconnect.
auto clearCallbacks() -> void;
}  // namespace HostApi

#endif  // ZWAVED_HOST_API_CALLBACK_DISPATCHER_HPP
