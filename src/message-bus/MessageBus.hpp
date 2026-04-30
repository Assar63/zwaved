#ifndef ZWAVED_MESSAGE_BUS_HPP
#define ZWAVED_MESSAGE_BUS_HPP

#include <cstdint>
#include <functional>
#include <string>

/**
 * In-process publish/subscribe bus for status broadcasts. Publishers
 * (currently the Z-Wave protocol layer) emit events that any number
 * of subscribers (e.g. external-API backends) can consume.
 *
 * Dispatch is synchronous on the publisher's thread; handlers must
 * return promptly and not block. The implementation is a thin
 * wrapper around eventpp; this header is intentionally minimal so
 * the underlying library can be replaced without touching call sites.
 */
namespace MessageBus
{
using SubscriptionId = std::uint64_t;

/// Lifecycle state of the Z-Wave dongle's serial attachment.
struct DongleStatus
{
    bool connected;
    std::string ttyPath;  // Empty unless connected.
};

[[nodiscard]] auto subscribe(const std::function<void(const DongleStatus&)>& handler) -> SubscriptionId;
auto unsubscribe(SubscriptionId subscriptionId) -> void;
auto publish(const DongleStatus& status) -> void;
}  // namespace MessageBus

#endif  // ZWAVED_MESSAGE_BUS_HPP
