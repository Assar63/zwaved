// WakeUpOrchestrator (#68) — the first of the src/orchestrator/ state
// machines. A sleeping node sends WAKE_UP_NOTIFICATION (Wake Up CC
// 0x84 cmd 0x07) and then stays awake for a short window waiting to
// hear from the controller. This orchestrator uses that window to
// drain any pending commands queued for the node, then tells it to go
// back to sleep with WAKE_UP_NO_MORE_INFORMATION (cmd 0x08) so it
// doesn't burn battery idling until its own timeout fires.
//
// Loose-coupling rule (same as every other module): it talks to the
// rest of the daemon over MessageBus only. It owns no thread and reacts
// synchronously to the WakeUpNotification bus event — which the
// cc-translator republishes from the inbound ApplicationCommand. The
// handler runs as a nested dispatch under the recursive bus mutex (see
// MessageBus.hpp); the SendDataCommand / SendWakeUpNoMoreInformation /
// WakeUpCycleComplete events it publishes are delivered depth-first
// before control returns to the translator.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../pending-queue/PendingQueue.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
// Replayed queue entries carry no callback correlation — the pending
// queue stores opaque payload bytes, not the originating request's
// callback id. 0 is the Serial API's "no callback requested" sentinel.
constexpr std::uint8_t NO_CALLBACK = 0x00;

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public member reads like a struct
struct State
{
    MessageBus::SubscriptionGuard wakeUpSub;  // auto-unsubscribes on teardown

    State() = default;
    ~State()
    {
        Logger::info("[WakeUpOrchestrator] shutdown complete");
    }

    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> State&
{
    static State instance;
    return instance;
}

auto onWakeUpNotification(const MessageBus::WakeUpNotification& note) -> void
{
    const std::uint8_t nodeId = note.sourceNodeId;

    // Pop everything queued for this node (priority asc, sequence asc).
    // An empty result is normal and still drives the sleep nudge below.
    std::vector<std::vector<std::uint8_t>> payloads = PendingQueue::instance().drain(nodeId);

    for (auto& payload : payloads)
    {
        MessageBus::publish(MessageBus::SendDataCommand{
            .nodeId     = nodeId,
            .payload    = std::move(payload),
            .callbackId = NO_CALLBACK,
        });
    }

    // Send the node back to sleep ASAP — whether or not we had anything
    // for it. Without this the node idles until its own (multi-second)
    // timeout, draining battery.
    MessageBus::publish(MessageBus::SendWakeUpNoMoreInformationCommand{
        .nodeId     = nodeId,
        .callbackId = NO_CALLBACK,
    });

    const auto drained = static_cast<std::uint32_t>(payloads.size());
    MessageBus::publish(MessageBus::WakeUpCycleComplete{
        .nodeId       = nodeId,
        .drainedCount = drained,
    });

    Logger::info("[WakeUpOrchestrator] node " + std::to_string(nodeId) + " woke — drained " + std::to_string(drained) +
                 " command(s), sent back to sleep");
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startWakeUpOrchestrator() -> void
{
    state().wakeUpSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::WakeUpNotification>(onWakeUpNotification));
    Logger::info("[WakeUpOrchestrator] watching for wake-ups");
}
}  // namespace
