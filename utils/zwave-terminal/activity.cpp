#include "activity.hpp"

#include "constants.hpp"

#include <array>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>

namespace zwt
{
namespace
{
// File-local: timestamp prefix for activity-log lines.
auto formatTimestamp() -> std::string
{
    const std::time_t epoch = std::time(nullptr);
    std::tm local{};
    ::localtime_r(&epoch, &local);
    std::array<char, TIMESTAMP_BUFFER_BYTES> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", &local);
    return {buffer.data()};
}
}  // namespace

auto activity() -> ActivityState&
{
    static ActivityState instance;
    return instance;
}

auto logLine(const std::string& message) -> void
{
    const std::string entry = formatTimestamp() + "  " + message;
    std::scoped_lock const lock(activity().mutex);
    activity().log.push_back(entry);
    while (activity().log.size() > MAX_LOG_LINES)
    {
        activity().log.pop_front();
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal/method
auto setDaemonError(std::uint8_t severity,
                    const std::string& source,
                    std::uint8_t code,
                    const std::string& message) -> void
{
    std::scoped_lock const lock(activity().mutex);
    activity().daemonError = DaemonErrorState{.severity = severity, .source = source, .code = code, .message = message};
}

auto setDongleStatus(bool connected, const std::string& path) -> void
{
    std::scoped_lock const lock(activity().mutex);
    activity().dongleConnected = connected;
    activity().donglePath      = path;
}

auto setDskPending(std::uint8_t nodeId, const std::string& dsk) -> void
{
    std::scoped_lock const lock(activity().mutex);
    activity().dskPending = DskPendingState{.nodeId = nodeId, .dsk = dsk, .active = !dsk.empty()};
}
}  // namespace zwt
