#include "HostApiCallbackDispatcher.hpp"

#include "HostApi.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace
{
struct State
{
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<HostApi::Callback> queue;
};

auto state() -> State&
{
    static State instance;
    return instance;
}

template <typename T> auto enqueue(const T& value) -> void
{
    {
        std::lock_guard<std::mutex> const lock(state().mutex);
        state().queue.emplace_back(value);
    }
    state().cv.notify_one();
}
}  // namespace

auto HostApi::publishCallback(const NodeStatusCallback& callback) -> void
{
    enqueue(callback);
}

auto HostApi::publishCallback(const SendDataCallback& callback) -> void
{
    enqueue(callback);
}

auto HostApi::popCallback(const std::atomic<bool>& stopFlag, const int timeoutMs) -> std::optional<Callback>
{
    std::unique_lock<std::mutex> lock(state().mutex);
    state().cv.wait_for(
        lock, std::chrono::milliseconds(timeoutMs), [&] { return !state().queue.empty() || !stopFlag.load(); });
    if (state().queue.empty())
    {
        return std::nullopt;
    }
    Callback out = state().queue.front();
    state().queue.pop_front();
    return out;
}

auto HostApi::wakeAllCallbacks() -> void
{
    state().cv.notify_all();
}

auto HostApi::clearCallbacks() -> void
{
    std::lock_guard<std::mutex> const lock(state().mutex);
    state().queue.clear();
}
