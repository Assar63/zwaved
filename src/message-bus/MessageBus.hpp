#ifndef ZWAVED_MESSAGE_BUS_HPP
#define ZWAVED_MESSAGE_BUS_HPP

#include <cstdint>
#include <functional>
#include <utility>

/**
 * In-process publish/subscribe bus for the daemon. Every cross-module
 * conversation rides this bus — state broadcasts, command requests
 * from external transports, and protocol-layer callbacks back to those
 * transports. The bus is the only seam between the threads in
 * src/zwave-protocol, src/zwave-dongle, src/external-api, and
 * src/node-registry; modules do not include each other's headers.
 *
 * Dispatch is synchronous on the publisher's thread; handlers must
 * return promptly and not block. State events flagged retained
 * (see IsRetained) are cached: a new subscriber receives the most
 * recent published value once, immediately, on subscribe — so late
 * starters don't miss the current state. Handlers must not call
 * publish() or subscribe() reentrantly.
 *
 * Adding a new event type:
 *   1. Add the event under `events:` in InterfaceManifest.yml.
 *   2. Rebuild — `MessageBus.gen.hpp` and the explicit
 *      instantiations in `MessageBus.gen.cpp` are emitted by
 *      scripts/codegen/.
 *
 * Event struct definitions and IsRetained<T> specializations live in
 * the generated `MessageBus.gen.hpp`; this header carries the public
 * subscribe / publish / unsubscribe / touch API.
 */
namespace MessageBus
{
using SubscriptionId = std::uint64_t;
}

// IWYU pragma: begin_exports
#include "MessageBus.gen.hpp"  // event/struct definitions + IsRetained<T>
// IWYU pragma: end_exports

namespace MessageBus
{
template <typename T> [[nodiscard]] auto subscribe(std::function<void(const T&)> handler) -> SubscriptionId;

template <typename T> auto publish(const T& value) -> void;

auto unsubscribe(SubscriptionId subscriptionId) -> void;

/// Force-construct the bus's internal singleton state. Each module that
/// uses MessageBus has its own static singleton whose destructor must
/// outlive the bus — calling `touch()` from the earliest constructor
/// (Logger, priority 101) registers the bus's atexit handler before any
/// module's, so by LIFO destruction the bus is the **last** static
/// teardown to run and joining-thread destructors can still safely
/// call `unsubscribe(...)`.
auto touch() -> void;

/// RAII wrapper around `SubscriptionId`. Unsubscribes on destruction so
/// a forgotten teardown path can't leak a subscription. Move-only —
/// subscription IDs are unique identities, not copies.
class SubscriptionGuard
{
  public:
    SubscriptionGuard() = default;
    // NOLINTNEXTLINE(readability-identifier-length): `id` is the conventional name for a SubscriptionId at this seam
    explicit SubscriptionGuard(SubscriptionId id)
        : id_(id)
    {
    }

    ~SubscriptionGuard()
    {
        if (id_ != 0)
        {
            unsubscribe(id_);
        }
    }

    SubscriptionGuard(const SubscriptionGuard&)                    = delete;
    auto operator=(const SubscriptionGuard&) -> SubscriptionGuard& = delete;
    SubscriptionGuard(SubscriptionGuard&& other) noexcept
        : id_(std::exchange(other.id_, 0))
    {
    }
    auto operator=(SubscriptionGuard&& other) noexcept -> SubscriptionGuard&
    {
        if (this != &other)
        {
            if (id_ != 0)
            {
                unsubscribe(id_);
            }
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }

  private:
    SubscriptionId id_ = 0;
};
}  // namespace MessageBus

#endif  // ZWAVED_MESSAGE_BUS_HPP
