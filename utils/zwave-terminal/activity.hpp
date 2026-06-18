#ifndef ZWAVE_TERMINAL_ACTIVITY_HPP
#define ZWAVE_TERMINAL_ACTIVITY_HPP

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

// Shared UI state for the zwave-terminal client: the activity log, the
// current dongle status, and the latest retained DaemonError. See #111.
namespace zwt
{
// Latest operator-visible daemon error (the retained DaemonError feed).
// An empty `message` means "no current problem" — banner hidden.
struct DaemonErrorState
{
    std::uint8_t severity = 0;
    std::string source;
    std::uint8_t code = 0;
    std::string message;
};

// A pending S2 DSK confirmation (#187): the daemon is waiting for the operator
// to enter the 5-digit PIN for `nodeId`; `dsk` is the partial DSK shown for
// label-matching. `active` clears when the daemon publishes the empty
// DSKPendingConfirmation (PIN accepted / session ended).
struct DskPendingState
{
    std::uint8_t nodeId = 0;
    std::string dsk;
    bool active = false;
};

struct ActivityState
{
    std::mutex mutex;
    std::deque<std::string> log;
    bool dongleConnected{false};
    std::string donglePath;
    DaemonErrorState daemonError;
    DskPendingState dskPending;
    // Set by D-Bus signal handlers (event-loop thread), consumed by the UI loop
    // (main thread) to refresh the node model — atomic so no lock is needed.
    std::atomic<bool> nodesDirty{false};   // list changed (incl/excl/remove-failed)
    std::atomic<bool> valuesDirty{false};  // a node value changed (NodeValueChanged)
};

[[nodiscard]] auto activity() -> ActivityState&;

auto logLine(const std::string& message) -> void;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal/method
auto setDaemonError(std::uint8_t severity,
                    const std::string& source,
                    std::uint8_t code,
                    const std::string& message) -> void;

auto setDongleStatus(bool connected, const std::string& path) -> void;

// Record / clear a pending S2 DSK confirmation (an empty `dsk` clears it).
auto setDskPending(std::uint8_t nodeId, const std::string& dsk) -> void;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_ACTIVITY_HPP
