#ifndef ZWAVED_HOST_API_SESSION_HPP
#define ZWAVED_HOST_API_SESSION_HPP

#include "HostApi.hpp"

#include <cstdint>
#include <optional>

namespace HostApi
{
/// Tracks the currently active inclusion/exclusion session. The Z-Wave
/// Host API permits at most one in-progress add/remove operation; the
/// session struct is kept simple but the API does not preclude future
/// multiplexing.
struct ActiveSession
{
    uint8_t commandId   = 0;  // 0x4A or 0x4B
    SessionId sessionId = 0;
};

/// True if `status` for `commandId` is a final state (success/failure/not-primary)
/// and should end the active session.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] auto isTerminalStatus(uint8_t commandId, uint8_t status) -> bool;

class SessionTracker
{
  public:
    SessionTracker() = default;

    [[nodiscard]] auto active() const -> std::optional<ActiveSession>;
    auto begin(uint8_t commandId, SessionId sessionId) -> void;
    auto end() -> void;

  private:
    std::optional<ActiveSession> current;
};
}  // namespace HostApi

#endif  // ZWAVED_HOST_API_SESSION_HPP
