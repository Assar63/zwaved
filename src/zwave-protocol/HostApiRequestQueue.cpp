#include "HostApiRequestQueue.hpp"

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
    std::deque<HostApi::Request> queue;
};

auto state() -> State&
{
    static State instance;
    return instance;
}
}  // namespace

auto HostApi::pushRequest(const Request& request) -> void
{
    {
        std::scoped_lock const lock(state().mutex);
        state().queue.push_back(request);
    }
    state().cv.notify_one();
}

auto HostApi::popRequest(const std::atomic<bool>& stopFlag, const int timeoutMs) -> std::optional<Request>
{
    std::unique_lock<std::mutex> lock(state().mutex);
    state().cv.wait_for(
        lock, std::chrono::milliseconds(timeoutMs), [&] { return !state().queue.empty() || !stopFlag.load(); });
    if (state().queue.empty())
    {
        return std::nullopt;
    }
    Request out = state().queue.front();
    state().queue.pop_front();
    return out;
}

auto HostApi::wakeAll() -> void
{
    state().cv.notify_all();
}

auto HostApi::clear() -> void
{
    std::scoped_lock const lock(state().mutex);
    state().queue.clear();
}
