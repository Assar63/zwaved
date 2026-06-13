// SecurityS2BootstrapOrchestrator (#187) — runs the Security S2 (CC 0x9F)
// inclusion key-exchange handshake so a freshly-included node ends up sharing
// the per-class network keys. Bus-only, like the other orchestrators
// (constructor priority 204).
//
// This is the *plaintext key-agreement* phase (+ DSK ritual). When an included
// node's NIF advertises CC 0x9F and the S2 network keys exist:
//   1. KEX_GET                         (plaintext) -> node replies KEX_REPORT
//   2. grant = requested ∩ supported (deny Access Control to a CSA node);
//      KEX_SET with the granted classes -> node sends its PUBLIC_KEY_REPORT
//   3. We reply with our PUBLIC_KEY_REPORT (un-obfuscated). For an
//      Authenticated / Access Control grant the node obfuscated the first DSK
//      group, so we raise DSKPendingConfirmation and wait for the operator's
//      ConfirmDsk (the 5-digit PIN off the device label), which restores the
//      obfuscated key bytes. For S2 Unauthenticated there's no PIN.
//   4. Once we hold the node's full public key, compute the ECDH shared secret
//      and derive the temporary bootstrap-channel keys (CKDF-TempExtract/
//      Expand, #202), and publish S2KeyAgreementComplete.
//
// Deferred to later #187 layers (all unverifiable until hardware, #189):
//   - the encrypted temp-channel phase: KEX echo verify (where a wrong-but-
//     well-formed DSK PIN finally surfaces as KEX_FAIL), per-class
//     NETWORK_KEY_REPORT install, NETWORK_KEY_VERIFY, TRANSFER_END — over SPAN
//     + AES-128-CCM (needs the SPAN runtime nonce sync, #199);
//   - the terminal DSK prompt UX (the D-Bus surface lands here).
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
#include "../zwave-protocol/security/s2/Encapsulation.hpp"
#include "../zwave-protocol/security/s2/Kex.hpp"
#include "../zwave-protocol/security/s2/KeyDerivation.hpp"
#include "../zwave-protocol/security/s2/NonceSync.hpp"
#include "../zwave-protocol/security/s2/PublicKey.hpp"
#include "../zwave-protocol/security/s2/SpanManager.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
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

constexpr std::uint8_t MESSAGE_ENCAPSULATION = 0x03;  // S2 command byte for the encrypted wrapper
constexpr std::size_t ENCAP_SEQ_OFFSET       = 2;     // [CC][cmd][seq] — seq the AAD binds

enum class Phase : std::uint8_t
{
    AwaitKexReport,
    AwaitPublicKey,
    AwaitDsk,            // grant needs a DSK PIN — parked until the operator confirms
    AwaitTempChannel,    // temp keys derived; awaiting the node's NONCE_GET (step 13)
    AwaitKexSetEcho,     // temp SPAN up; awaiting the encrypted KEX_SET echo (step 16)
    AwaitNetworkKeyGet,  // echoes verified; awaiting per-class NETWORK_KEY_GET (next layer, steps 20+)
};

struct Session
{
    Phase phase = Phase::AwaitKexReport;
    S2::Crypto::KeyPair keyPair{};
    std::uint8_t grantedKeys = 0;
    S2::Crypto::PublicKey nodePublicKey{};  // the joining node's key (DSK-obfuscated until the PIN is applied)
    S2::KeyDerivation::TempKeys tempKeys{};
    S2::Kex::Set sentKexSet{};            // step 5, kept to verify the echo in step 16
    S2::Kex::Report receivedKexReport{};  // step 3, echoed back in step 18
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionGuard includedSub;
    MessageBus::SubscriptionGuard appCmdSub;
    MessageBus::SubscriptionGuard confirmDskSub;
    MessageBus::SubscriptionGuard dongleSub;
    MessageBus::SubscriptionGuard keysSub;
    std::map<std::uint8_t, Session> sessions;  // per-node handshake progress
    S2::SpanManager spanManager;               // per-peer temp-channel SPAN runtime
    std::array<std::uint8_t, 4> homeId{};
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
    session.grantedKeys       = granted;
    session.receivedKexReport = *report;  // echoed back over the temp channel in step 18
    session.sentKexSet        = S2::Kex::Set{.echo           = false,
                                             .requestCsa     = false,
                                             .selectedScheme = S2::Kex::KEX_SCHEME_1,
                                             .selectedCurve  = S2::Kex::ECDH_CURVE25519,
                                             .grantedKeys    = granted};
    session.phase             = Phase::AwaitPublicKey;
    sendPlaintext(nodeId, S2::Kex::encodeSet(session.sentKexSet));
}

// Retract a pending DSK prompt (empty value = "nothing pending"), mirroring the
// DaemonError "recovered" convention.
auto clearDskPrompt() -> void
{
    MessageBus::publish(MessageBus::DSKPendingConfirmation{.nodeId = 0, .dsk = {}});
}

// Final step shared by the Unauthenticated and DSK-confirmed paths: ECDH against
// the node's (de-obfuscated) public key, derive the temp channel keys, advance.
auto finishKeyAgreement(std::uint8_t nodeId, Session& session) -> void
{
    const auto shared = S2::Crypto::ecdh(session.keyPair.privateKey, session.nodePublicKey);
    if (!shared.has_value())
    {
        Logger::warn("[s2-bootstrap] ECDH failed for node " + std::to_string(nodeId) + " — KEX_FAIL");
        sendPlaintext(nodeId, S2::Kex::encodeFail(S2::Kex::FAIL_DECRYPT));
        state().sessions.erase(nodeId);
        return;
    }
    session.tempKeys = S2::KeyDerivation::deriveTempKeys(*shared, session.keyPair.publicKey, session.nodePublicKey);
    session.phase    = Phase::AwaitTempChannel;

    // Arm the temp-channel SPAN runtime: the bootstrap's encrypted frames ride
    // the TempKeyCCM + TempPersonalizationString until the real class keys land.
    state().spanManager.configurePeer(nodeId,
                                      S2::SpanManager::PeerConfig{.classKey        = session.tempKeys.keyCcm,
                                                                  .personalization = session.tempKeys.personalization,
                                                                  .homeId          = state().homeId,
                                                                  .ourNodeId       = state().controllerNodeId,
                                                                  .peerNodeId      = nodeId});

    Logger::info("[s2-bootstrap] node " + std::to_string(nodeId) +
                 " key agreement complete — temporary channel keys derived");
    MessageBus::publish(MessageBus::S2KeyAgreementComplete{.nodeId = nodeId});
}

auto onPublicKeyReport(std::uint8_t nodeId, Session& session, std::span<const std::uint8_t> ccData) -> void
{
    const auto report = S2::PublicKey::decode(ccData);
    if (!report.has_value())
    {
        return;
    }
    session.nodePublicKey = report->key;
    // Answer with our (controller, including-node) public key, un-obfuscated.
    sendPlaintext(nodeId, S2::PublicKey::encode(true, session.keyPair.publicKey, S2::PublicKey::OBFUSCATE_NONE));

    if ((session.grantedKeys & DSK_REQUIRED_KEYS) != 0)
    {
        // Authenticated / Access Control: the node obfuscated its leading key
        // bytes (the DSK PIN). Park until the operator supplies it via
        // ConfirmDSK; the partial DSK (first group shown as 00000) lets them
        // match the prompt against the device label.
        session.phase = Phase::AwaitDsk;
        Logger::info("[s2-bootstrap] node " + std::to_string(nodeId) +
                     " granted an authenticated class — awaiting operator DSK confirmation");
        MessageBus::publish(
            MessageBus::DSKPendingConfirmation{.nodeId = nodeId, .dsk = S2::PublicKey::dskString(report->key)});
        return;
    }

    // S2 Unauthenticated: no obfuscation, so we can finish key agreement now.
    finishKeyAgreement(nodeId, session);
}

auto onConfirmDsk(const MessageBus::ConfirmDsk& event) -> void
{
    const auto session = state().sessions.find(event.nodeId);
    if (session == state().sessions.end() || session->second.phase != Phase::AwaitDsk)
    {
        return;  // no node is waiting on a DSK for this id
    }
    const auto pin = S2::PublicKey::parsePin(event.pin);
    if (!pin.has_value())
    {
        Logger::warn("[s2-bootstrap] node " + std::to_string(event.nodeId) +
                     " DSK confirmation ignored — PIN must be exactly 5 digits");
        return;  // leave the prompt up for a retry
    }
    // Restore the obfuscated leading key bytes from the PIN, then agree keys. A
    // wrong-but-well-formed PIN yields the wrong shared secret and only surfaces
    // when the encrypted KEX echo fails to authenticate (a later #187 layer).
    session->second.nodePublicKey = S2::PublicKey::applyPin(session->second.nodePublicKey, *pin);
    Logger::info("[s2-bootstrap] node " + std::to_string(event.nodeId) + " DSK confirmed — resuming key agreement");
    clearDskPrompt();
    finishKeyAgreement(event.nodeId, session->second);
}

// Wrap `inner` in a temp-key MESSAGE_ENCAPSULATION via the SPAN runtime and send
// it raw (a 0x9F frame ProtocolThread forwards verbatim). Drops + logs if no
// SPAN is established — shouldn't happen mid-handshake.
auto sendEncrypted(std::uint8_t nodeId, std::span<const std::uint8_t> inner) -> void
{
    auto frame = state().spanManager.encrypt(nodeId, inner);
    if (!frame.has_value())
    {
        Logger::warn("[s2-bootstrap] node " + std::to_string(nodeId) + " no temp SPAN to encrypt — dropping frame");
        return;
    }
    sendPlaintext(nodeId, std::move(*frame));
}

// Decrypt an inbound temp-channel MESSAGE_ENCAPSULATION (instantiating the SPAN
// from its SPAN extension on first use), returning the inner CC command.
auto decryptInbound(std::uint8_t nodeId,
                    const Session& session,
                    std::span<const std::uint8_t> frame) -> std::optional<std::vector<std::uint8_t>>
{
    const auto nonce = state().spanManager.receiveNonce(nodeId, frame);
    if (!nonce.has_value())
    {
        return std::nullopt;
    }
    const S2::Encapsulation::Context context{.senderNodeId   = nodeId,
                                             .receiverNodeId = state().controllerNodeId,
                                             .homeId         = state().homeId,
                                             .sequenceNumber = frame[ENCAP_SEQ_OFFSET]};
    return S2::Encapsulation::decrypt(frame, context, session.tempKeys.keyCcm, *nonce);
}

// Steps 16-18: verify the encrypted KEX_SET echo is identical to the KEX_SET we
// sent in step 5 (MITM check, A3), then echo the node's KEX_REPORT back over the
// temp channel (step 18). On mismatch, abort with an encrypted KEX_FAIL.
auto onKexSetEcho(std::uint8_t nodeId, Session& session, std::span<const std::uint8_t> frame) -> void
{
    const auto inner = decryptInbound(nodeId, session, frame);
    if (!inner.has_value())
    {
        Logger::warn("[s2-bootstrap] node " + std::to_string(nodeId) + " temp-channel decrypt failed");
        return;  // a real impl resyncs the SPAN (SOS); the blind path just waits
    }
    const auto echoed = S2::Kex::decodeSet(std::span<const std::uint8_t>(*inner));
    if (!echoed.has_value() || echoed->selectedScheme != session.sentKexSet.selectedScheme ||
        echoed->selectedCurve != session.sentKexSet.selectedCurve ||
        echoed->grantedKeys != session.sentKexSet.grantedKeys)
    {
        Logger::warn("[s2-bootstrap] node " + std::to_string(nodeId) + " KEX_SET echo mismatch — KEX_FAIL");
        const auto fail = S2::Kex::encodeFail(S2::Kex::FAIL_AUTH);
        sendEncrypted(nodeId, std::span<const std::uint8_t>(fail));
        state().sessions.erase(nodeId);
        return;
    }
    S2::Kex::Report reportEcho = session.receivedKexReport;
    reportEcho.echo            = true;
    const auto reportFrame     = S2::Kex::encodeReport(reportEcho);
    sendEncrypted(nodeId, std::span<const std::uint8_t>(reportFrame));
    session.phase = Phase::AwaitNetworkKeyGet;
    Logger::info("[s2-bootstrap] node " + std::to_string(nodeId) +
                 " KEX echoes verified — temporary secure channel authenticated");
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
        break;  // waiting on the operator's ConfirmDSK
    case Phase::AwaitTempChannel:
        if (*command == S2::NonceSync::NONCE_GET)
        {
            // Steps 13-14: the node asks for our nonce; reply in plaintext so it
            // can establish its send SPAN over the temp channel.
            sendPlaintext(event.sourceNodeId, state().spanManager.respondToNonceGet(event.sourceNodeId));
            session->second.phase = Phase::AwaitKexSetEcho;
        }
        break;
    case Phase::AwaitKexSetEcho:
        if (*command == MESSAGE_ENCAPSULATION)
        {
            onKexSetEcho(event.sourceNodeId, session->second, ccData);
        }
        break;
    case Phase::AwaitNetworkKeyGet:
        break;  // per-class NETWORK_KEY_GET install — the next #187 layer
    }
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startSecurityS2BootstrapOrchestrator() -> void
{
    state().includedSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeIncluded>(onNodeIncluded));
    state().appCmdSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(onApplicationCommand));
    state().confirmDskSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ConfirmDsk>(onConfirmDsk));
    state().dongleSub     = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleInfo>(
        [](const MessageBus::DongleInfo& info) -> void
        {
            state().controllerNodeId = info.controllerNodeId;
            std::copy_n(
                info.homeId.begin(), std::min(info.homeId.size(), state().homeId.size()), state().homeId.begin());
        }));
    state().keysSub       = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::S2NetworkKeysReady>(
        [](const MessageBus::S2NetworkKeysReady& event) -> void { state().keysReady = event.ready; }));
}
}  // namespace
