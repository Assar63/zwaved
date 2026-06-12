// SecurityBootstrapOrchestrator (#167) — runs the Security S0 (CC 0x98)
// inclusion bootstrap so a freshly-included node ends up sharing the network
// key. Bus-only, like the other orchestrators (constructor priority 204).
//
// Sequence when an included node's NIF advertises CC 0x98:
//   1. SCHEME_GET  (plaintext)            -> node replies SCHEME_REPORT
//   2. NONCE_GET   (plaintext)            -> node replies NONCE_REPORT
//   3. NETWORK_KEY_SET, the real 16-byte network key, S0-encrypted under the
//      *temporary* all-zero key + the node's nonce -> node installs it and
//      replies NETWORK_KEY_VERIFY, encrypted under the *real* key.
//   4. The inbound decrypt seam (#166) decrypts VERIFY (we already answered the
//      node's NONCE_GET via the NonceResponder) and republishes [0x98, 0x07];
//      on seeing it we mark the node secure and publish NodeSecurityStatus.
//
// Steps 1-2 and the NONCE_REPORT for step 3 are plaintext, so this composes
// with the existing nonce + decrypt machinery rather than re-implementing it.
//
// MVP limits (deferred): no inactivity timeout (a stalled bootstrap just lingers
// in its map entry — a bus-only reactor has no timer); single scheme (S0);
// SCHEME_REPORT contents aren't validated. Verified on real hardware in #168.
//
// NOTE: wire correctness here is unverified against a physical S0 device until
// #168 — the unit tests pin the sequence + the temp-key encryption shape.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../node-registry/NodeRegistry.hpp"
#include "../zwave-protocol/security/s0/Encapsulation.hpp"
#include "../zwave-protocol/security/s0/NetworkKey.hpp"
#include "../zwave-protocol/security/s0/NonceTable.hpp"
#include "../zwave-protocol/security/s0/Security.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <algorithm>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint8_t CC_SECURITY = 0x98;
constexpr std::uint8_t NO_CALLBACK = 0x00;

enum class Phase : std::uint8_t
{
    AwaitSchemeReport,
    AwaitNonceReport,
    AwaitVerify,
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionGuard includedSub;
    MessageBus::SubscriptionGuard appCmdSub;
    MessageBus::SubscriptionGuard dongleSub;
    std::map<std::uint8_t, Phase> sessions;  // per-node bootstrap progress
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

auto sendPlaintext(std::uint8_t nodeId, std::vector<std::uint8_t> payload) -> void
{
    MessageBus::publish(
        MessageBus::SendDataCommand{.nodeId = nodeId, .payload = std::move(payload), .callbackId = NO_CALLBACK});
}

auto onNodeIncluded(const MessageBus::NodeIncluded& event) -> void
{
    const auto& ccs = event.commandClasses;
    if (std::find(ccs.begin(), ccs.end(), CC_SECURITY) == ccs.end())
    {
        return;  // not a secure node — nothing to bootstrap
    }
    if (!S0::NetworkKey::current().has_value())
    {
        Logger::warn("[s0-bootstrap] node " + std::to_string(event.nodeId) +
                     " supports S0 but no network key is available — skipping secure bootstrap");
        return;
    }
    Logger::info("[s0-bootstrap] node " + std::to_string(event.nodeId) + " supports S0 — starting bootstrap");
    state().sessions[event.nodeId] = Phase::AwaitSchemeReport;
    sendPlaintext(event.nodeId, {CC_SECURITY, Security::SECURITY_SCHEME_GET, 0x00});  // 0 = no schemes from controller
}

// Step 3: encrypt NETWORK_KEY_SET (the real key) under the all-zero temporary
// key + the node's just-reported nonce, and send it.
auto sendNetworkKeySet(std::uint8_t nodeId, const S0::Nonce& nodeNonce) -> void
{
    const auto realKey = S0::NetworkKey::current();
    if (!realKey.has_value())
    {
        Logger::warn("[s0-bootstrap] network key vanished mid-bootstrap for node " + std::to_string(nodeId));
        state().sessions.erase(nodeId);
        return;
    }
    std::vector<std::uint8_t> inner{CC_SECURITY, Security::SECURITY_NETWORK_KEY_SET};
    inner.insert(inner.end(), realKey->begin(), realKey->end());

    const S0::Crypto::Key tempKey{};  // all-zero temporary key used only for KEY_SET
    const auto frame = S0::Encapsulation::encrypt(
        std::span<const std::uint8_t>(inner), state().controllerNodeId, nodeId, S0::randomNonce(), nodeNonce, tempKey);
    MessageBus::publish(MessageBus::SendDataCommand{.nodeId = nodeId, .payload = frame, .callbackId = NO_CALLBACK});
}

auto onApplicationCommand(const MessageBus::ApplicationCommand& event) -> void
{
    const auto session = state().sessions.find(event.sourceNodeId);
    if (session == state().sessions.end())
    {
        return;  // no bootstrap in flight for this node
    }
    const auto command = Security::commandByte(event.ccData);
    if (!command.has_value())
    {
        return;
    }

    switch (session->second)
    {
    case Phase::AwaitSchemeReport:
        if (*command == Security::SECURITY_SCHEME_REPORT)
        {
            session->second = Phase::AwaitNonceReport;
            sendPlaintext(event.sourceNodeId, Security::encodeNonceGet());
        }
        break;
    case Phase::AwaitNonceReport:
        if (*command == Security::SECURITY_NONCE_REPORT)
        {
            const auto nodeNonce = Security::decodeNonceReport(std::span<const std::uint8_t>(event.ccData));
            if (!nodeNonce.has_value())
            {
                return;
            }
            session->second = Phase::AwaitVerify;
            sendNetworkKeySet(event.sourceNodeId, *nodeNonce);
        }
        break;
    case Phase::AwaitVerify:
        if (*command == Security::SECURITY_NETWORK_KEY_VERIFY)
        {
            NodeRegistry::setSecure(event.sourceNodeId, true);
            MessageBus::publish(MessageBus::NodeSecurityStatus{.nodeId = event.sourceNodeId, .secure = true});
            Logger::info("[s0-bootstrap] node " + std::to_string(event.sourceNodeId) +
                         " verified — secure bootstrap complete");
            state().sessions.erase(session);
        }
        break;
    }
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startSecurityBootstrapOrchestrator() -> void
{
    state().includedSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeIncluded>(onNodeIncluded));
    state().appCmdSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(onApplicationCommand));
    state().dongleSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleInfo>(
        [](const MessageBus::DongleInfo& info) -> void { state().controllerNodeId = info.controllerNodeId; }));
}
}  // namespace
