#ifndef ZWAVED_HOST_API_CALLBACK_DISPATCHER_HPP
#define ZWAVED_HOST_API_CALLBACK_DISPATCHER_HPP

#include "HostApi.hpp"

#include <atomic>
#include <optional>
#include <variant>

namespace HostApi
{
/// Variant covering every callback the protocol thread may publish
/// upward to the external API thread. NodeStatus covers Add/Remove
/// progression; SendData covers application-layer transmission
/// completion (SwitchBinary SET, etc.).
using Callback = std::variant<NodeStatusCallback, SendDataCallback>;

/// Push a decoded callback to consumers (the external API thread).
auto publishCallback(const NodeStatusCallback& callback) -> void;
auto publishCallback(const SendDataCallback& callback) -> void;

/// Block waiting for a callback, up to timeoutMs. Returns std::nullopt
/// if the wait expires or stopFlag becomes false.
[[nodiscard]] auto popCallback(const std::atomic<bool>& stopFlag, int timeoutMs) -> std::optional<Callback>;

/// Wake any blocked consumer. Used during shutdown.
auto wakeAllCallbacks() -> void;

/// Clear any pending callbacks; used after a serial reconnect.
auto clearCallbacks() -> void;
}  // namespace HostApi

#endif  // ZWAVED_HOST_API_CALLBACK_DISPATCHER_HPP
