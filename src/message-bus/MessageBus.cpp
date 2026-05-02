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
template <typename T> struct Topic
{
    eventpp::CallbackList<void(const T&)> callbacks;
    std::optional<T> retained;  // populated only for retained event types
};

// Per-event-type singleton. The instance() function provides per-T
// storage without the topics having to be aggregated into a single
// State struct, which would force MessageBus.cpp to be edited every
// time a new event is added (just adding an instantiation block at the
// bottom suffices). One shared mutex serializes all topics so that
// publish/subscribe interleave atomically — the cost is negligible at
// our event rates and keeps the replay-on-subscribe sequencing simple.
struct BusState
{
    std::mutex mutex;
    std::atomic<MessageBus::SubscriptionId> nextId{1};
    std::unordered_map<MessageBus::SubscriptionId, std::function<void()>> removers;
};

auto bus() -> BusState&
{
    static BusState instance;
    return instance;
}

template <typename T> auto topic() -> Topic<T>&
{
    static Topic<T> instance;
    return instance;
}
}  // namespace

template <typename T> auto MessageBus::subscribe(std::function<void(const T&)> handler) -> SubscriptionId
{
    std::scoped_lock const lock(bus().mutex);
    auto& topicRef             = topic<T>();
    const auto handle          = topicRef.callbacks.append(handler);
    const SubscriptionId newId = bus().nextId.fetch_add(1, std::memory_order_relaxed);
    bus().removers.emplace(newId, [handle]() { topic<T>().callbacks.remove(handle); });
    if constexpr (IsRetained<T>::value)
    {
        if (const auto cached = topicRef.retained; cached.has_value())
        {
            handler(*cached);
        }
    }
    return newId;
}

template <typename T> auto MessageBus::publish(const T& value) -> void
{
    std::scoped_lock const lock(bus().mutex);
    auto& topicRef = topic<T>();
    if constexpr (IsRetained<T>::value)
    {
        topicRef.retained = value;
    }
    topicRef.callbacks(value);
}

auto MessageBus::touch() -> void
{
    static_cast<void>(bus());
}

auto MessageBus::unsubscribe(SubscriptionId subscriptionId) -> void
{
    std::function<void()> remover;
    {
        std::scoped_lock const lock(bus().mutex);
        const auto iter = bus().removers.find(subscriptionId);
        if (iter == bus().removers.end())
        {
            return;
        }
        remover = std::move(iter->second);
        bus().removers.erase(iter);
    }
    remover();
}

// ---- Explicit instantiations -------------------------------------------
// One pair per event type. Add new types here when extending MessageBus.hpp.

namespace MessageBus
{
template auto subscribe<DongleStatus>(std::function<void(const DongleStatus&)>) -> SubscriptionId;
template auto publish<DongleStatus>(const DongleStatus&) -> void;
template auto subscribe<DongleInfo>(std::function<void(const DongleInfo&)>) -> SubscriptionId;
template auto publish<DongleInfo>(const DongleInfo&) -> void;
template auto subscribe<InitData>(std::function<void(const InitData&)>) -> SubscriptionId;
template auto publish<InitData>(const InitData&) -> void;
template auto subscribe<NodeListChanged>(std::function<void(const NodeListChanged&)>) -> SubscriptionId;
template auto publish<NodeListChanged>(const NodeListChanged&) -> void;
template auto subscribe<LoggerConfig>(std::function<void(const LoggerConfig&)>) -> SubscriptionId;
template auto publish<LoggerConfig>(const LoggerConfig&) -> void;
template auto subscribe<DonglesConfig>(std::function<void(const DonglesConfig&)>) -> SubscriptionId;
template auto publish<DonglesConfig>(const DonglesConfig&) -> void;
template auto subscribe<StorageConfig>(std::function<void(const StorageConfig&)>) -> SubscriptionId;
template auto publish<StorageConfig>(const StorageConfig&) -> void;
template auto subscribe<BehaviorConfig>(std::function<void(const BehaviorConfig&)>) -> SubscriptionId;
template auto publish<BehaviorConfig>(const BehaviorConfig&) -> void;
template auto subscribe<ApplicationCommand>(std::function<void(const ApplicationCommand&)>) -> SubscriptionId;
template auto publish<ApplicationCommand>(const ApplicationCommand&) -> void;
template auto subscribe<NodeInclusionStatus>(std::function<void(const NodeInclusionStatus&)>) -> SubscriptionId;
template auto publish<NodeInclusionStatus>(const NodeInclusionStatus&) -> void;
template auto subscribe<NodeExclusionStatus>(std::function<void(const NodeExclusionStatus&)>) -> SubscriptionId;
template auto publish<NodeExclusionStatus>(const NodeExclusionStatus&) -> void;
template auto subscribe<SendDataCallback>(std::function<void(const SendDataCallback&)>) -> SubscriptionId;
template auto publish<SendDataCallback>(const SendDataCallback&) -> void;
template auto subscribe<SessionStatus>(std::function<void(const SessionStatus&)>) -> SubscriptionId;
template auto publish<SessionStatus>(const SessionStatus&) -> void;
template auto subscribe<RemoveFailedNodeStatus>(std::function<void(const RemoveFailedNodeStatus&)>) -> SubscriptionId;
template auto publish<RemoveFailedNodeStatus>(const RemoveFailedNodeStatus&) -> void;
template auto subscribe<AddNodeCommand>(std::function<void(const AddNodeCommand&)>) -> SubscriptionId;
template auto publish<AddNodeCommand>(const AddNodeCommand&) -> void;
template auto subscribe<RemoveNodeCommand>(std::function<void(const RemoveNodeCommand&)>) -> SubscriptionId;
template auto publish<RemoveNodeCommand>(const RemoveNodeCommand&) -> void;
template auto subscribe<RemoveFailedNodeCommand>(std::function<void(const RemoveFailedNodeCommand&)>) -> SubscriptionId;
template auto publish<RemoveFailedNodeCommand>(const RemoveFailedNodeCommand&) -> void;
template auto subscribe<SetSwitchBinaryCommand>(std::function<void(const SetSwitchBinaryCommand&)>) -> SubscriptionId;
template auto publish<SetSwitchBinaryCommand>(const SetSwitchBinaryCommand&) -> void;
template auto subscribe<GetSwitchBinaryCommand>(std::function<void(const GetSwitchBinaryCommand&)>) -> SubscriptionId;
template auto publish<GetSwitchBinaryCommand>(const GetSwitchBinaryCommand&) -> void;
template auto subscribe<SetBasicCommand>(std::function<void(const SetBasicCommand&)>) -> SubscriptionId;
template auto publish<SetBasicCommand>(const SetBasicCommand&) -> void;
template auto subscribe<GetBasicCommand>(std::function<void(const GetBasicCommand&)>) -> SubscriptionId;
template auto publish<GetBasicCommand>(const GetBasicCommand&) -> void;
template auto subscribe<SetAssociationCommand>(std::function<void(const SetAssociationCommand&)>) -> SubscriptionId;
template auto publish<SetAssociationCommand>(const SetAssociationCommand&) -> void;
template auto subscribe<RemoveAssociationCommand>(std::function<void(const RemoveAssociationCommand&)>)
    -> SubscriptionId;
template auto publish<RemoveAssociationCommand>(const RemoveAssociationCommand&) -> void;
template auto subscribe<GetAssociationCommand>(std::function<void(const GetAssociationCommand&)>) -> SubscriptionId;
template auto publish<GetAssociationCommand>(const GetAssociationCommand&) -> void;
template auto subscribe<GetAssociationGroupingsCommand>(std::function<void(const GetAssociationGroupingsCommand&)>)
    -> SubscriptionId;
template auto publish<GetAssociationGroupingsCommand>(const GetAssociationGroupingsCommand&) -> void;
template auto subscribe<SetMultichannelAssociationCommand>(
    std::function<void(const SetMultichannelAssociationCommand&)>) -> SubscriptionId;
template auto publish<SetMultichannelAssociationCommand>(const SetMultichannelAssociationCommand&) -> void;
template auto subscribe<RemoveMultichannelAssociationCommand>(
    std::function<void(const RemoveMultichannelAssociationCommand&)>) -> SubscriptionId;
template auto publish<RemoveMultichannelAssociationCommand>(const RemoveMultichannelAssociationCommand&) -> void;
template auto subscribe<GetMultichannelAssociationCommand>(
    std::function<void(const GetMultichannelAssociationCommand&)>) -> SubscriptionId;
template auto publish<GetMultichannelAssociationCommand>(const GetMultichannelAssociationCommand&) -> void;
template auto subscribe<GetMultichannelAssociationGroupingsCommand>(
    std::function<void(const GetMultichannelAssociationGroupingsCommand&)>) -> SubscriptionId;
template auto publish<GetMultichannelAssociationGroupingsCommand>(const GetMultichannelAssociationGroupingsCommand&)
    -> void;
}  // namespace MessageBus
