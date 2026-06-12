// SecurityOutboundOrchestrator (#175) — encrypt-on-send for S0-secure nodes.
//
// ProtocolThread's pushSendData diverts any non-Security payload bound for a
// secure node here (as a SecureSendRequest) instead of sending it raw. We then
// run the S0 outbound dance: ask the node for a nonce (SECURITY_NONCE_GET),
// and when it answers (SECURITY_NONCE_REPORT) encrypt the payload under the
// network key + that nonce and hand the wrapper back as a SendDataCommand
// (a CC 0x98 frame, which pushSendData sends raw — that's what avoids a loop).
//
// One nonce per message: OFB reuses its keystream for a given key+IV, so
// encrypting two payloads under the same nonce would be a two-time-pad break.
// We therefore serialise per node — encrypt the front payload on each
// NONCE_REPORT and, if more remain queued, fetch a fresh nonce for the next.
//
// MVP limits (deferred): no inactivity timeout (a bus-only reactor has no
// timer — a node that never answers NONCE_GET leaves its queue pending); no
// retransmit. Verified end-to-end on hardware in #168.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../zwave-protocol/security/s0/Encapsulation.hpp"
#include "../zwave-protocol/security/s0/NetworkKey.hpp"
#include "../zwave-protocol/security/s0/NonceTable.hpp"
#include "../zwave-protocol/security/s0/Security.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint8_t NO_CALLBACK = 0x00;

struct Pending
{
    std::vector<std::uint8_t> payload;
    std::uint8_t callbackId = 0;
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionGuard requestSub;
    MessageBus::SubscriptionGuard appCmdSub;
    MessageBus::SubscriptionGuard dongleSub;
    std::map<std::uint8_t, std::deque<Pending>> queues;  // per-node outbound, FIFO
    std::uint8_t controllerNodeId = 0;

    State()                                    = default;
    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
    ~State()                                   = default;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> State&
{
    static State instance;
    return instance;
}

auto requestNonce(std::uint8_t nodeId) -> void
{
    MessageBus::publish(MessageBus::SendDataCommand{
        .nodeId = nodeId, .payload = Security::encodeNonceGet(), .callbackId = NO_CALLBACK});
}

auto onSecureSendRequest(const MessageBus::SecureSendRequest& event) -> void
{
    auto& queue        = state().queues[event.nodeId];
    const bool wasIdle = queue.empty();
    queue.push_back({.payload = event.payload, .callbackId = event.callbackId});
    if (wasIdle)
    {
        // No nonce request in flight for this node — kick one off. (If one is
        // already pending, this payload rides the serialised chain.)
        requestNonce(event.nodeId);
    }
}

auto onApplicationCommand(const MessageBus::ApplicationCommand& event) -> void
{
    const auto command = Security::commandByte(event.ccData);
    if (!command.has_value() || *command != Security::SECURITY_NONCE_REPORT)
    {
        return;
    }
    const auto queueIter = state().queues.find(event.sourceNodeId);
    if (queueIter == state().queues.end() || queueIter->second.empty())
    {
        return;  // an unsolicited / bootstrap nonce report — not ours
    }
    const auto nodeNonce = Security::decodeNonceReport(std::span<const std::uint8_t>(event.ccData));
    const auto key       = S0::NetworkKey::current();
    if (!nodeNonce.has_value() || !key.has_value())
    {
        Logger::warn("[s0-outbound] dropping queued secure send(s) for node " + std::to_string(event.sourceNodeId) +
                     " — bad nonce report or no network key");
        state().queues.erase(queueIter);
        return;
    }

    // Encrypt exactly one payload per nonce (OFB keystream must not repeat).
    const Pending pending = std::move(queueIter->second.front());
    queueIter->second.pop_front();
    const auto frame = S0::Encapsulation::encrypt(std::span<const std::uint8_t>(pending.payload),
                                                  state().controllerNodeId,
                                                  event.sourceNodeId,
                                                  S0::randomNonce(),
                                                  *nodeNonce,
                                                  *key);
    MessageBus::publish(
        MessageBus::SendDataCommand{.nodeId = event.sourceNodeId, .payload = frame, .callbackId = pending.callbackId});

    if (!queueIter->second.empty())
    {
        requestNonce(event.sourceNodeId);  // fresh nonce for the next queued payload
    }
    else
    {
        state().queues.erase(queueIter);
    }
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startSecurityOutboundOrchestrator() -> void
{
    state().requestSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SecureSendRequest>(onSecureSendRequest));
    state().appCmdSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(onApplicationCommand));
    state().dongleSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleInfo>(
        [](const MessageBus::DongleInfo& info) -> void { state().controllerNodeId = info.controllerNodeId; }));
}
}  // namespace
