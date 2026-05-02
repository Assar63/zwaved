#ifndef ZWAVED_MESSAGE_BUS_INTERNAL_HPP
#define ZWAVED_MESSAGE_BUS_INTERNAL_HPP

#include "MessageBus.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <eventpp/callbacklist.h>

// Implementation details for MessageBus.cpp and the generator-emitted
// MessageBus.gen.cpp. Both translation units #include this header so
// the explicit instantiations in the generated TU see the full
// template bodies. Don't include from outside the MessageBus
// implementation -- consumers should rely on MessageBus.hpp's public
// API instead.
//
// Helpers live in `MessageBus::detail` (rather than an anonymous
// namespace as before) so the static locals inside `bus()` and
// `topic<T>()` are merged across both TUs into a single program-
// wide singleton, instead of one copy per TU.

namespace MessageBus::detail
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

inline auto bus() -> BusState&
{
    static BusState instance;
    return instance;
}

template <typename T> auto topic() -> Topic<T>&
{
    static Topic<T> instance;
    return instance;
}
}  // namespace MessageBus::detail

template <typename T> auto MessageBus::subscribe(std::function<void(const T&)> handler) -> SubscriptionId
{
    using detail::bus;
    using detail::topic;
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
    using detail::bus;
    using detail::topic;
    std::scoped_lock const lock(bus().mutex);
    auto& topicRef = topic<T>();
    if constexpr (IsRetained<T>::value)
    {
        topicRef.retained = value;
    }
    topicRef.callbacks(value);
}

#endif  // ZWAVED_MESSAGE_BUS_INTERNAL_HPP
