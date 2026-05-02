#include "MessageBus.hpp"

#include "MessageBusInternal.hpp"

#include <functional>
#include <mutex>
#include <utility>

// Template definitions for subscribe<T> / publish<T> live in
// MessageBusInternal.hpp; the explicit per-event instantiations live in
// the generated MessageBus.gen.cpp. This translation unit is left
// with the non-templated public entry points only.

auto MessageBus::touch() -> void
{
    static_cast<void>(detail::bus());
}

auto MessageBus::unsubscribe(SubscriptionId subscriptionId) -> void
{
    std::function<void()> remover;
    {
        std::scoped_lock const lock(detail::bus().mutex);
        const auto iter = detail::bus().removers.find(subscriptionId);
        if (iter == detail::bus().removers.end())
        {
            return;
        }
        remover = std::move(iter->second);
        detail::bus().removers.erase(iter);
    }
    remover();
}
