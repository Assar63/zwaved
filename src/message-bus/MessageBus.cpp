#include "MessageBus.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <eventpp/callbacklist.h>

namespace
{
using DongleStatusList = eventpp::CallbackList<void(const MessageBus::DongleStatus&)>;

struct State
{
    DongleStatusList dongleStatus;
    std::mutex registryMutex;
    std::atomic<MessageBus::SubscriptionId> nextId{1};
    std::unordered_map<MessageBus::SubscriptionId, std::function<void()>> removers;
};

auto state() -> State&
{
    static State instance;
    return instance;
}

auto registerRemover(std::function<void()> remover) -> MessageBus::SubscriptionId
{
    const MessageBus::SubscriptionId newId = state().nextId.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> const lock(state().registryMutex);
    state().removers.emplace(newId, std::move(remover));
    return newId;
}
}  // namespace

auto MessageBus::subscribe(const std::function<void(const DongleStatus&)>& handler) -> SubscriptionId
{
    const auto handle = state().dongleStatus.append(handler);
    return registerRemover([handle]() { state().dongleStatus.remove(handle); });
}

auto MessageBus::unsubscribe(SubscriptionId subscriptionId) -> void
{
    std::function<void()> remover;
    {
        std::lock_guard<std::mutex> const lock(state().registryMutex);
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
    state().dongleStatus(status);
}
