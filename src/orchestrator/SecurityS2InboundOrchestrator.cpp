// SecurityS2InboundOrchestrator (#199) — the inbound Security S2 (CC 0x9F)
// decapsulation seam + nonce establishment for nodes already bootstrapped to an
// S2 class (the analog of the S0 inbound seam in cc-translator/Encapsulation.cpp
// and the S0 NonceResponder). Bus-only, constructor priority 204.
//
// For a frame from an S2-secure node (NodeRegistry::isSecure — in-progress
// inclusions aren't secure yet, so the bootstrap orchestrator owns those):
//   - SECURITY_2_NONCE_GET  -> reply with a NONCE_REPORT carrying a fresh REI so
//     the node can build its send SPAN (we keep the REI for decap).
//   - MESSAGE_ENCAPSULATION -> derive the CCM nonce from the shared transport
//     SPAN (instantiating it from the frame's SPAN extension on first use),
//     decrypt under the node's class KeyCCM, and republish the inner CC frame as
//     a fresh ApplicationCommand so the normal decoders run. A decap failure
//     drives a resync (a fresh NONCE_REPORT with SOS).
//   - SECURITY_2_NONCE_REPORT -> fed to the shared SPAN manager for the outbound
//     path (which lands in a later #199 layer).
//
// The transport SPAN state is process-wide (S2::Transport::manager()) so the
// outbound encrypt path reuses the SPANs established here. Unverified against a
// physical S2 device until #189.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../node-registry/NodeRegistry.hpp"
#include "../zwave-protocol/security/s2/Encapsulation.hpp"
#include "../zwave-protocol/security/s2/NonceSync.hpp"
#include "../zwave-protocol/security/s2/Transport.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint8_t CC_SECURITY_2         = 0x9F;
constexpr std::uint8_t MESSAGE_ENCAPSULATION = 0x03;
constexpr std::uint8_t NO_CALLBACK           = 0x00;
constexpr std::size_t ENCAP_SEQ_OFFSET       = 2;  // [CC][cmd][seq]

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionGuard appCmdSub;
    MessageBus::SubscriptionGuard dongleSub;
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

auto sendPlaintext(std::uint8_t nodeId, std::vector<std::uint8_t> payload) -> void
{
    MessageBus::publish(
        MessageBus::SendDataCommand{.nodeId = nodeId, .payload = std::move(payload), .callbackId = NO_CALLBACK});
}

// Configure (or refresh) the node's transport SPAN peer from its recorded S2
// class; returns the class KeyCCM for decryption, or std::nullopt if the node
// isn't S2-secure or the keys aren't loaded.
auto configurePeer(std::uint8_t nodeId) -> std::optional<S2::Crypto::Key>
{
    if (!NodeRegistry::isSecure(nodeId))
    {
        return std::nullopt;  // an in-progress inclusion — the bootstrap orchestrator owns it
    }
    const auto derived =
        S2::Transport::resolveClassKeys(static_cast<std::uint8_t>(NodeRegistry::securityScheme(nodeId)));
    if (!derived.has_value())
    {
        return std::nullopt;  // S0-secure, or the S2 keys aren't available
    }
    S2::Transport::manager().configurePeer(nodeId,
                                           S2::SpanManager::PeerConfig{.classKey        = derived->keyCcm,
                                                                       .personalization = derived->personalization,
                                                                       .homeId          = state().homeId,
                                                                       .ourNodeId       = state().controllerNodeId,
                                                                       .peerNodeId      = nodeId});
    return derived->keyCcm;
}

auto onApplicationCommand(const MessageBus::ApplicationCommand& event) -> void
{
    const auto& data = event.ccData;
    if (data.size() < 2 || data[0] != CC_SECURITY_2)
    {
        return;
    }
    const std::uint8_t nodeId = event.sourceNodeId;
    const auto classKeyCcm    = configurePeer(nodeId);
    if (!classKeyCcm.has_value())
    {
        return;
    }
    const std::span<const std::uint8_t> frame{data};

    if (data[1] == S2::NonceSync::NONCE_GET)
    {
        // The node is establishing its send SPAN — hand it a fresh REI.
        sendPlaintext(nodeId, S2::Transport::manager().respondToNonceGet(nodeId));
        return;
    }
    // NONCE_REPORT (the node's answer to a NONCE_GET *we* sent) belongs to the
    // outbound path — SecurityS2OutboundOrchestrator handles it.
    if (data[1] != MESSAGE_ENCAPSULATION)
    {
        return;
    }

    const auto nonce = S2::Transport::manager().receiveNonce(nodeId, frame);
    if (!nonce.has_value())
    {
        // No SPAN yet — ask the node to resync (SOS) so its next frame carries a
        // SPAN extension we can build from.
        Logger::warn("[s2-inbound] node " + std::to_string(nodeId) + " encap with no SPAN — requesting resync");
        sendPlaintext(nodeId, S2::Transport::manager().respondToNonceGet(nodeId));
        return;
    }
    const S2::Encapsulation::Context context{.senderNodeId   = nodeId,
                                             .receiverNodeId = state().controllerNodeId,
                                             .homeId         = state().homeId,
                                             .sequenceNumber = data[ENCAP_SEQ_OFFSET]};
    const auto inner = S2::Encapsulation::decrypt(frame, context, *classKeyCcm, *nonce);
    if (!inner.has_value())
    {
        Logger::warn("[s2-inbound] node " + std::to_string(nodeId) + " decap failed — requesting resync");
        sendPlaintext(nodeId, S2::Transport::manager().respondToNonceGet(nodeId));
        return;
    }
    // Re-feed the plaintext CC so the normal decoders (and the D-Bus raw re-emit)
    // run, exactly as the S0 seam does.
    MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = nodeId, .ccData = *inner});
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startSecurityS2InboundOrchestrator() -> void
{
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
