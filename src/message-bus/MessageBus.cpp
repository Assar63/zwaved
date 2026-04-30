#include "MessageBus.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

#include <eventpp/callbacklist.h>

namespace
{
using DongleStatusList       = eventpp::CallbackList<void(const MessageBus::DongleStatus&)>;
using DongleInfoList         = eventpp::CallbackList<void(const MessageBus::DongleInfo&)>;
using ApplicationCommandList = eventpp::CallbackList<void(const MessageBus::ApplicationCommand&)>;

// State events (currently just DongleStatus) are retained so that late
// subscribers — components that come up after the publisher has already
// announced its first state — see the latest value on subscribe. Both
// publish() and subscribe() take the same mutex, so a subscriber added
// during a concurrent publish is observed atomically: it either sees the
// publish via the callback list (if appended first) or via the replay
// (if the cache update completed first), never both out of order.
//
// Transient events (ApplicationCommand) are not retained — late
// subscribers do not get historic events.
struct State
{
    DongleStatusList dongleStatus;
    DongleInfoList dongleInfo;
    ApplicationCommandList applicationCommand;
    std::mutex stateMutex;
    std::atomic<MessageBus::SubscriptionId> nextId{1};
    std::unordered_map<MessageBus::SubscriptionId, std::function<void()>> removers;
    std::optional<MessageBus::DongleStatus> lastDongleStatus;
    std::optional<MessageBus::DongleInfo> lastDongleInfo;
};

auto state() -> State&
{
    static State instance;
    return instance;
}
}  // namespace

auto MessageBus::subscribe(const std::function<void(const DongleStatus&)>& handler) -> SubscriptionId
{
    std::lock_guard<std::mutex> const lock(state().stateMutex);
    const auto handle          = state().dongleStatus.append(handler);
    const SubscriptionId newId = state().nextId.fetch_add(1, std::memory_order_relaxed);
    state().removers.emplace(newId, [handle]() { state().dongleStatus.remove(handle); });
    if (const auto cached = state().lastDongleStatus; cached.has_value())
    {
        handler(*cached);
    }
    return newId;
}

auto MessageBus::unsubscribe(SubscriptionId subscriptionId) -> void
{
    std::function<void()> remover;
    {
        std::lock_guard<std::mutex> const lock(state().stateMutex);
        const auto iter = state().removers.find(subscriptionId);
        if (iter == state().removers.end())
        {
            return;
        }
        remover = std::move(iter->second);
        state().removers.erase(iter);
    }
    remover();
}

auto MessageBus::publish(const DongleStatus& status) -> void
{
    std::lock_guard<std::mutex> const lock(state().stateMutex);
    state().lastDongleStatus = status;
    state().dongleStatus(status);
}

auto MessageBus::subscribe(const std::function<void(const DongleInfo&)>& handler) -> SubscriptionId
{
    std::lock_guard<std::mutex> const lock(state().stateMutex);
    const auto handle          = state().dongleInfo.append(handler);
    const SubscriptionId newId = state().nextId.fetch_add(1, std::memory_order_relaxed);
    state().removers.emplace(newId, [handle]() { state().dongleInfo.remove(handle); });
    if (const auto cached = state().lastDongleInfo; cached.has_value())
    {
        handler(*cached);
    }
    return newId;
}

auto MessageBus::publish(const DongleInfo& info) -> void
{
    std::lock_guard<std::mutex> const lock(state().stateMutex);
    state().lastDongleInfo = info;
    state().dongleInfo(info);
}

auto MessageBus::subscribe(const std::function<void(const ApplicationCommand&)>& handler) -> SubscriptionId
{
    std::lock_guard<std::mutex> const lock(state().stateMutex);
    const auto handle          = state().applicationCommand.append(handler);
    const SubscriptionId newId = state().nextId.fetch_add(1, std::memory_order_relaxed);
    state().removers.emplace(newId, [handle]() { state().applicationCommand.remove(handle); });
    return newId;
}

auto MessageBus::publish(const ApplicationCommand& event) -> void
{
    std::lock_guard<std::mutex> const lock(state().stateMutex);
    state().applicationCommand(event);
}
