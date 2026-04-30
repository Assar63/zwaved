#ifndef ZWAVED_HOST_API_REQUEST_QUEUE_HPP
#define ZWAVED_HOST_API_REQUEST_QUEUE_HPP

#include "HostApi.hpp"

#include <atomic>
#include <optional>
#include <variant>

namespace HostApi
{
/// Variant covering every request the external API may submit to the
/// protocol thread. Add and Remove cover both initiation (mode 0x01..)
/// and stop (mode 0x05) requests via the same struct.
using Request = std::variant<AddNodeRequest, RemoveNodeRequest>;

/// Push a new request. Wakes any thread blocked in popRequest().
auto pushRequest(const Request& request) -> void;

/// Block waiting for a request, up to timeoutMs. Returns std::nullopt
/// if the wait expires or stopFlag becomes false.
[[nodiscard]] auto popRequest(const std::atomic<bool>& stopFlag, int timeoutMs) -> std::optional<Request>;

/// Wake any thread currently blocked in popRequest. Used during shutdown.
auto wakeAll() -> void;

/// Clear any queued requests; used after a serial reconnect.
auto clear() -> void;
}  // namespace HostApi

#endif  // ZWAVED_HOST_API_REQUEST_QUEUE_HPP
