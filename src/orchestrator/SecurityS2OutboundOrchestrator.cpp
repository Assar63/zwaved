// SecurityS2OutboundOrchestrator (#199) — encrypt-on-send for S2-secure nodes,
// the S2 analog of SecurityOutboundOrchestrator (#175). Bus-only, priority 204.
//
// ProtocolThread's pushSendData diverts a non-Security payload bound for an
// S2-secure node here (as a SecureS2SendRequest). We encrypt it through the
// shared transport SPAN (S2::Transport::manager(), the same one the inbound seam
// fills) and hand the MESSAGE_ENCAPSULATION wrapper back as a SendDataCommand (a
// 0x9F frame, which pushSendData sends raw — that's what avoids a loop).
//
// If no SPAN is established yet we can't encrypt, so we ask the node for a nonce
// (SECURITY_2_NONCE_GET) and queue the payload; its NONCE_REPORT carries the REI
// we need, after which the first encrypt instantiates the SPAN (riding our SEI in
// a SPAN extension) and the rest reuse it. Unlike S0/OFB, S2 draws a fresh CCM
// nonce from the SPAN per frame, so once a SPAN exists we can encrypt back-to-back
// without another exchange.
//
// MVP limits (deferred): no inactivity timeout (a node that never answers
// NONCE_GET leaves its queue pending); SPAN persistence across restart is a
// later #199 item. Unverified against a physical S2 device until #189.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../node-registry/NodeRegistry.hpp"
#include "../zwave-protocol/security/s2/NonceSync.hpp"
#include "../zwave-protocol/security/s2/Transport.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
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
    std::map<std::uint8_t, std::deque<Pending>> queues;  // per-node outbound, FIFO, awaiting a SPAN
    std::array<std::uint8_t, 4> homeId{};
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

// Configure the node's transport SPAN peer from its recorded S2 class; false if
// the node isn't S2-secure or the keys aren't loaded.
auto configurePeer(std::uint8_t nodeId) -> bool
{
    if (!NodeRegistry::isSecure(nodeId))
    {
        return false;
    }
    const auto derived =
        S2::Transport::resolveClassKeys(static_cast<std::uint8_t>(NodeRegistry::securityScheme(nodeId)));
    if (!derived.has_value())
    {
        return false;
    }
    S2::Transport::manager().configurePeer(nodeId,
                                           S2::SpanManager::PeerConfig{.classKey        = derived->keyCcm,
                                                                       .personalization = derived->personalization,
                                                                       .homeId          = state().homeId,
                                                                       .ourNodeId       = state().controllerNodeId,
                                                                       .peerNodeId      = nodeId});
    return true;
}

auto sendEncrypted(std::uint8_t nodeId, const Pending& pending) -> bool
{
    auto frame = S2::Transport::manager().encrypt(nodeId, std::span<const std::uint8_t>(pending.payload));
    if (!frame.has_value())
    {
        return false;  // no SPAN + no REI yet
    }
    MessageBus::publish(
        MessageBus::SendDataCommand{.nodeId = nodeId, .payload = std::move(*frame), .callbackId = pending.callbackId});
    return true;
}

auto onSecureS2SendRequest(const MessageBus::SecureS2SendRequest& event) -> void
{
    if (!configurePeer(event.nodeId))
    {
        Logger::warn("[s2-outbound] dropping secure send for node " + std::to_string(event.nodeId) +
                     " — not S2-secure or keys unavailable");
        return;
    }
    const Pending pending{.payload = event.payload, .callbackId = event.callbackId};

    // Fast path: a SPAN is already established (e.g. from inbound traffic).
    if (sendEncrypted(event.nodeId, pending))
    {
        return;
    }

    // Otherwise queue it and (if we're the first) kick off a nonce exchange.
    auto& queue        = state().queues[event.nodeId];
    const bool wasIdle = queue.empty();
    queue.push_back(pending);
    if (wasIdle)
    {
        MessageBus::publish(MessageBus::SendDataCommand{.nodeId     = event.nodeId,
                                                        .payload    = S2::Transport::manager().nonceGet(event.nodeId),
                                                        .callbackId = NO_CALLBACK});
    }
}

auto onApplicationCommand(const MessageBus::ApplicationCommand& event) -> void
{
    const auto command = S2::NonceSync::commandByte(std::span<const std::uint8_t>(event.ccData));
    if (!command.has_value() || *command != S2::NonceSync::NONCE_REPORT)
    {
        return;
    }
    const auto queueIter = state().queues.find(event.sourceNodeId);
    if (queueIter == state().queues.end() || queueIter->second.empty())
    {
        return;  // a NONCE_REPORT we weren't waiting on (e.g. resync for the inbound path)
    }
    const auto report = S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(event.ccData));
    if (!report.has_value())
    {
        return;
    }
    S2::Transport::manager().acceptNonceReport(event.sourceNodeId, *report);

    // The first encrypt instantiates the SPAN (REI + a fresh SEI in a SPAN
    // extension); the rest reuse it. Drain the whole queue.
    while (!queueIter->second.empty())
    {
        if (!sendEncrypted(event.sourceNodeId, queueIter->second.front()))
        {
            Logger::warn("[s2-outbound] node " + std::to_string(event.sourceNodeId) +
                         " still no SPAN after NONCE_REPORT — dropping queued secure send(s)");
            break;
        }
        queueIter->second.pop_front();
    }
    state().queues.erase(queueIter);
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startSecurityS2OutboundOrchestrator() -> void
{
    state().requestSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SecureS2SendRequest>(onSecureS2SendRequest));
    state().appCmdSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(onApplicationCommand));
    state().dongleSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleInfo>(
        [](const MessageBus::DongleInfo& info) -> void
        {
            state().controllerNodeId = info.controllerNodeId;
            std::copy_n(
                info.homeId.begin(), std::min(info.homeId.size(), state().homeId.size()), state().homeId.begin());
        }));
}
}  // namespace
