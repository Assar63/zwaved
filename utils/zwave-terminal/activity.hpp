#ifndef ZWAVE_TERMINAL_ACTIVITY_HPP
#define ZWAVE_TERMINAL_ACTIVITY_HPP

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

struct ActivityState
{
    std::mutex mutex;
    std::deque<std::string> log;
    bool dongleConnected{false};
    std::string donglePath;
    DaemonErrorState daemonError;
};

[[nodiscard]] auto activity() -> ActivityState&;

auto logLine(const std::string& message) -> void;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal/method
auto setDaemonError(std::uint8_t severity,
                    const std::string& source,
                    std::uint8_t code,
                    const std::string& message) -> void;

auto setDongleStatus(bool connected, const std::string& path) -> void;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_ACTIVITY_HPP
