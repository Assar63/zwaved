// SecurityS2BootstrapOrchestrator (#187) — runs the Security S2 (CC 0x9F)
// inclusion key-exchange handshake so a freshly-included node ends up sharing
// the per-class network keys. Bus-only, like the other orchestrators
// (constructor priority 204).
//
// This is the *plaintext key-agreement* phase — the first #187 layer. When an
// included node's NIF advertises CC 0x9F and the S2 network keys exist:
//   1. KEX_GET                         (plaintext) -> node replies KEX_REPORT
//   2. grant = requested ∩ supported (deny Access Control to a CSA node);
//      KEX_SET with the granted classes -> node sends its PUBLIC_KEY_REPORT
//   3. We reply with our PUBLIC_KEY_REPORT (un-obfuscated), then — for a grant
//      that needs no DSK (S2 Unauthenticated only) — compute the ECDH shared
//      secret and derive the temporary bootstrap-channel keys
//      (CKDF-TempExtract/Expand, #202), and publish S2KeyAgreementComplete.
//
// Deferred to later #187 layers (all unverifiable until hardware, #189):
//   - the DSK ritual for Authenticated / Access Control (the node obfuscates
//     the first key bytes; the operator types the PIN). Here a DSK-requiring
//     grant parks the session at AwaitDsk and goes no further;
//   - the encrypted temp-channel phase: KEX echo verify, per-class
//     NETWORK_KEY_REPORT install, NETWORK_KEY_VERIFY, TRANSFER_END — over SPAN
//     + AES-128-CCM (needs the SPAN runtime nonce sync, #199);
//   - the operator D-Bus / terminal progress + DSK confirmation surface.
//
// Single-session assumption: like the S0 bootstrap, concurrent inclusions are
// disambiguated only by source node id; there is no inactivity timeout (a
// stalled handshake just lingers in the session map).
//
// NOTE: wire correctness here is unverified against a physical S2 device until
// #189 — the unit tests pin the negotiation sequence + the grant policy.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../zwave-protocol/security/s2/Crypto.hpp"
#include "../zwave-protocol/security/s2/Kex.hpp"
#include "../zwave-protocol/security/s2/KeyDerivation.hpp"
#include "../zwave-protocol/security/s2/PublicKey.hpp"
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
constexpr std::uint8_t CC_SECURITY_2 = 0x9F;
constexpr std::uint8_t NO_CALLBACK   = 0x00;

// Classes this controller is willing to grant. S0 is bootstrapped by the
// separate S0 orchestrator, so it's not offered here.
constexpr std::uint8_t CONTROLLER_SUPPORTED_KEYS =
    S2::Kex::KEY_S2_UNAUTHENTICATED | S2::Kex::KEY_S2_AUTHENTICATED | S2::Kex::KEY_S2_ACCESS_CONTROL;

// Granted classes that oblige the DSK ritual (the joining node obfuscates the
// leading public-key bytes the operator must restore via the PIN).
constexpr std::uint8_t DSK_REQUIRED_KEYS = S2::Kex::KEY_S2_AUTHENTICATED | S2::Kex::KEY_S2_ACCESS_CONTROL;

enum class Phase : std::uint8_t
{
    AwaitKexReport,
    AwaitPublicKey,
    AwaitDsk,          // grant needs a DSK PIN — parked until the DSK layer lands
    AwaitTempChannel,  // temp keys derived; the encrypted phase takes over (later layer)
};

struct Session
{
    Phase phase = Phase::AwaitKexReport;
    S2::Crypto::KeyPair keyPair{};
    std::uint8_t grantedKeys = 0;
    S2::KeyDerivation::TempKeys tempKeys{};
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionGuard includedSub;
    MessageBus::SubscriptionGuard appCmdSub;
    MessageBus::SubscriptionGuard dongleSub;
    MessageBus::SubscriptionGuard keysSub;
    std::map<std::uint8_t, Session> sessions;  // per-node handshake progress
    std::uint8_t controllerNodeId = 0;
    bool keysReady                = false;

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
    if (std::find(ccs.begin(), ccs.end(), CC_SECURITY_2) == ccs.end())
    {
        return;  // not an S2 node — nothing to bootstrap
    }
    if (!state().keysReady)
    {
        Logger::warn("[s2-bootstrap] node " + std::to_string(event.nodeId) +
                     " supports S2 but the network keys aren't ready — skipping secure bootstrap");
        return;
    }
    Logger::info("[s2-bootstrap] node " + std::to_string(event.nodeId) + " supports S2 — starting key exchange");
    state().sessions[event.nodeId] = Session{.phase = Phase::AwaitKexReport, .keyPair = S2::Crypto::generateKeyPair()};
    sendPlaintext(event.nodeId, S2::Kex::encodeGet());
}

auto onKexReport(std::uint8_t nodeId, Session& session, std::span<const std::uint8_t> ccData) -> void
{
    const auto report = S2::Kex::decodeReport(ccData);
    if (!report.has_value())
    {
        return;
    }
    const auto granted = S2::Kex::grantKeys(*report, CONTROLLER_SUPPORTED_KEYS);
    if (granted == 0)
    {
        Logger::warn("[s2-bootstrap] node " + std::to_string(nodeId) + " requested no grantable key — KEX_FAIL");
        sendPlaintext(nodeId, S2::Kex::encodeFail(S2::Kex::FAIL_KEY));
        state().sessions.erase(nodeId);
        return;
    }
    session.grantedKeys = granted;
    session.phase       = Phase::AwaitPublicKey;
    sendPlaintext(nodeId,
                  S2::Kex::encodeSet(S2::Kex::Set{.echo           = false,
                                                  .requestCsa     = false,
                                                  .selectedScheme = S2::Kex::KEX_SCHEME_1,
                                                  .selectedCurve  = S2::Kex::ECDH_CURVE25519,
                                                  .grantedKeys    = granted}));
}

auto onPublicKeyReport(std::uint8_t nodeId, Session& session, std::span<const std::uint8_t> ccData) -> void
{
    const auto report = S2::PublicKey::decode(ccData);
    if (!report.has_value())
    {
        return;
    }
    // Answer with our (controller, including-node) public key, un-obfuscated.
    sendPlaintext(nodeId, S2::PublicKey::encode(true, session.keyPair.publicKey, S2::PublicKey::OBFUSCATE_NONE));

    if ((session.grantedKeys & DSK_REQUIRED_KEYS) != 0)
    {
        // Authenticated / Access Control: the node obfuscated its leading key
        // bytes. We need the operator's DSK PIN to restore them before ECDH —
        // that ritual is the next #187 layer, so park the session here.
        session.phase = Phase::AwaitDsk;
        Logger::info("[s2-bootstrap] node " + std::to_string(nodeId) +
                     " granted an authenticated class — DSK confirmation required (deferred)");
        return;
    }

    // S2 Unauthenticated: no obfuscation, so we can finish key agreement now.
    const auto shared = S2::Crypto::ecdh(session.keyPair.privateKey, report->key);
    if (!shared.has_value())
    {
        Logger::warn("[s2-bootstrap] ECDH failed for node " + std::to_string(nodeId) + " — KEX_FAIL");
        sendPlaintext(nodeId, S2::Kex::encodeFail(S2::Kex::FAIL_DECRYPT));
        state().sessions.erase(nodeId);
        return;
    }
    session.tempKeys = S2::KeyDerivation::deriveTempKeys(*shared, session.keyPair.publicKey, report->key);
    session.phase    = Phase::AwaitTempChannel;
    Logger::info("[s2-bootstrap] node " + std::to_string(nodeId) +
                 " key agreement complete — temporary channel keys derived");
    MessageBus::publish(MessageBus::S2KeyAgreementComplete{.nodeId = nodeId});
}

auto onApplicationCommand(const MessageBus::ApplicationCommand& event) -> void
{
    const auto session = state().sessions.find(event.sourceNodeId);
    if (session == state().sessions.end())
    {
        return;  // no S2 handshake in flight for this node
    }
    const auto command = S2::Kex::commandByte(std::span<const std::uint8_t>(event.ccData));
    if (!command.has_value())
    {
        return;
    }
    const std::span<const std::uint8_t> ccData{event.ccData};

    switch (session->second.phase)
    {
    case Phase::AwaitKexReport:
        if (*command == S2::Kex::KEX_REPORT)
        {
            onKexReport(event.sourceNodeId, session->second, ccData);
        }
        break;
    case Phase::AwaitPublicKey:
        if (*command == S2::PublicKey::PUBLIC_KEY_REPORT)
        {
            onPublicKeyReport(event.sourceNodeId, session->second, ccData);
        }
        break;
    case Phase::AwaitDsk:
    case Phase::AwaitTempChannel:
        break;  // handled by later #187 layers
    }
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startSecurityS2BootstrapOrchestrator() -> void
{
    state().includedSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeIncluded>(onNodeIncluded));
    state().appCmdSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(onApplicationCommand));
    state().dongleSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleInfo>(
        [](const MessageBus::DongleInfo& info) -> void { state().controllerNodeId = info.controllerNodeId; }));
    state().keysSub   = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::S2NetworkKeysReady>(
        [](const MessageBus::S2NetworkKeysReady& event) -> void { state().keysReady = event.ready; }));
}
}  // namespace
