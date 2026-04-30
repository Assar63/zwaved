#include "DeviceHandoff.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace
{
struct State
{
    std::mutex mutex;
    std::condition_variable cv;
    std::string path;
};

auto state() -> State&
{
    static State instance;
    return instance;
}
}  // namespace

auto DeviceHandoff::publish(const std::string& ttyPath) -> void
{
    {
        std::lock_guard<std::mutex> const lock(state().mutex);
        state().path = ttyPath;
    }
    state().cv.notify_all();
}

auto DeviceHandoff::clear() -> void
{
    {
        std::lock_guard<std::mutex> const lock(state().mutex);
        state().path.clear();
    }
    state().cv.notify_all();
}

auto DeviceHandoff::awaitDevicePath(const std::atomic<bool>& stopFlag) -> std::optional<std::string>
{
    std::unique_lock<std::mutex> lock(state().mutex);
    state().cv.wait(lock, [&] { return !state().path.empty() || !stopFlag.load(); });
    if (!stopFlag.load())
    {
        return std::nullopt;
    }
    return state().path;
}

auto DeviceHandoff::wakeAll() -> void
{
    state().cv.notify_all();
}

auto DeviceHandoff::current() -> std::string
{
    std::lock_guard<std::mutex> const lock(state().mutex);
    return state().path;
}
