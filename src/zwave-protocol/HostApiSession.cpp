#include "HostApiSession.hpp"

#include "HostApi.hpp"

#include <cstdint>
#include <optional>

namespace
{
constexpr uint8_t STATUS_DONE_INCLUSION = 0x06;  // Inclusion completed
constexpr uint8_t STATUS_FAILED_INCLUDE = 0x07;  // Inclusion failed
constexpr uint8_t STATUS_DONE_EXCLUSION = 0x06;  // Exclusion completed
constexpr uint8_t STATUS_FAILED_EXCLUDE = 0x07;  // Exclusion failed
constexpr uint8_t STATUS_NOT_PRIMARY    = 0x23;  // Not primary controller
}  // namespace

auto HostApi::SessionTracker::active() const -> std::optional<ActiveSession>
{
    return current;
}

auto HostApi::SessionTracker::begin(const uint8_t commandId, const SessionId sessionId) -> void
{
    current = ActiveSession{.commandId = commandId, .sessionId = sessionId};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): commandId/status both 8-bit by spec
auto HostApi::isTerminalStatus(const uint8_t commandId, const uint8_t status) -> bool
{
    if (status == STATUS_NOT_PRIMARY)
    {
        return true;
    }
    if (commandId == CMD_ADD_NODE_TO_NETWORK)
    {
        return status == STATUS_DONE_INCLUSION || status == STATUS_FAILED_INCLUDE;
    }
    if (commandId == CMD_REMOVE_NODE_FROM_NETWORK)
    {
        return status == STATUS_DONE_EXCLUSION || status == STATUS_FAILED_EXCLUDE;
    }
    return false;
}

auto HostApi::SessionTracker::end() -> void
{
    current.reset();
}
