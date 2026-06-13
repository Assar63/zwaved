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
//   5. Over the encrypted temp channel (SpanManager, temp KeyCCM): answer the
//      node's NONCE_GET, verify the KEX_SET echo (MITM check) and echo the
//      KEX_REPORT back (steps 13-18); then, per granted class, transfer the key
//      (NETWORK_KEY_REPORT), let the node prove install over a fresh class SPAN
//      (NETWORK_KEY_VERIFY), and close each with TRANSFER_END (steps 20-29).
//   6. On the node's final TRANSFER_END (Key Request Complete) mark it secure:
//      NodeRegistry::setSecurityScheme + NodeSecurityStatus (step 30).
//
// Deferred to later #187 layers (all unverifiable until hardware, #189):
//   - the terminal DSK prompt UX (the D-Bus surface already lands here);
//   - general (post-bootstrap) S2 transport: the inbound decrypt seam + SOS
//     resync wired through ProtocolThread (#199).
//
// Single-session assumption: like the S0 bootstrap, concurrent inclusions are
// disambiguated only by source node id; there is no inactivity timeout (a
// stalled handshake just lingers in the session map).
//
// NOTE: wire correctness here is unverified against a physical S2 device until
// #189 — the unit tests pin the negotiation sequence + the grant policy.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../node-registry/NodeRegistry.hpp"
#include "../zwave-protocol/security/s2/Crypto.hpp"
#include "../zwave-protocol/security/s2/Encapsulation.hpp"
#include "../zwave-protocol/security/s2/Kex.hpp"
#include "../zwave-protocol/security/s2/KeyDerivation.hpp"
#include "../zwave-protocol/security/s2/KeyInstall.hpp"
#include "../zwave-protocol/security/s2/NetworkKeys.hpp"
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
    AwaitNetworkKeyGet,  // echoes verified; awaiting NETWORK_KEY_GET or the final TRANSFER_END (steps 20/30)
    AwaitClassNonceGet,  // key transferred; awaiting the node's NONCE_GET for the new class SPAN (step 25)
    AwaitKeyVerify,      // class SPAN up; awaiting the NETWORK_KEY_VERIFY under the new key (step 27)
};

struct Session
{
    Phase phase = Phase::AwaitKexReport;
    S2::Crypto::KeyPair keyPair{};
    std::uint8_t grantedKeys = 0;
    S2::Crypto::PublicKey nodePublicKey{};  // the joining node's key (DSK-obfuscated until the PIN is applied)
    S2::KeyDerivation::TempKeys tempKeys{};
    S2::Kex::Set sentKexSet{};                             // step 5, kept to verify the echo in step 16
    S2::Kex::Report receivedKexReport{};                   // step 3, echoed back in step 18
    std::uint8_t installingKeyBit = 0;                     // the class key currently being installed (steps 20-29)
    S2::KeyDerivation::NetworkKeys installingClassKeys{};  // its derived KeyCCM + personalization
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
    S2::SpanManager classSpanManager;          // per-class verify-channel SPAN (reset per key)
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

// The highest secure class in a granted-key bitmask — the scheme we persist.
auto schemeFor(std::uint8_t grantedKeys) -> NodeRegistry::SecurityScheme
{
    if ((grantedKeys & S2::Kex::KEY_S2_ACCESS_CONTROL) != 0)
    {
        return NodeRegistry::SecurityScheme::S2AccessControl;
    }
    if ((grantedKeys & S2::Kex::KEY_S2_AUTHENTICATED) != 0)
    {
        return NodeRegistry::SecurityScheme::S2Authenticated;
    }
    return NodeRegistry::SecurityScheme::S2Unauthenticated;
}

// The raw class key answering a NETWORK_KEY_GET for `keyBit`, from the loaded set.
auto classKeyForBit(std::uint8_t keyBit) -> std::optional<S2::Crypto::Key>
{
    const auto cls  = S2::KeyInstall::classForKeyBit(keyBit);
    const auto keys = S2::NetworkKeys::current();
    if (!cls.has_value() || !keys.has_value())
    {
        return std::nullopt;
    }
    return S2::NetworkKeys::keyFor(*keys, *cls);
}

auto completeBootstrap(std::uint8_t nodeId, const Session& session) -> void
{
    const auto scheme = schemeFor(session.grantedKeys);
    NodeRegistry::setSecurityScheme(nodeId, scheme);
    MessageBus::publish(MessageBus::NodeSecurityStatus{.nodeId = nodeId, .scheme = static_cast<std::uint8_t>(scheme)});
    Logger::info("[s2-bootstrap] node " + std::to_string(nodeId) + " S2 bootstrap complete — node is secure");
    state().sessions.erase(nodeId);
}

// Steps 20-30 entry (temp channel): a frame here is either a NETWORK_KEY_GET
// (transfer the next granted class key) or the terminal TRANSFER_END.
auto onTempChannelKeyExchange(std::uint8_t nodeId, Session& session, std::span<const std::uint8_t> frame) -> void
{
    const auto inner = decryptInbound(nodeId, session, frame);
    if (!inner.has_value())
    {
        Logger::warn("[s2-bootstrap] node " + std::to_string(nodeId) + " temp-channel decrypt failed (key exchange)");
        return;
    }
    const std::span<const std::uint8_t> innerSpan{*inner};
    const auto command = S2::KeyInstall::commandByte(innerSpan);
    if (!command.has_value())
    {
        return;
    }
    if (*command == S2::KeyInstall::TRANSFER_END)
    {
        const auto end = S2::KeyInstall::decodeTransferEnd(innerSpan);
        if (end.has_value() && end->keyRequestComplete)
        {
            completeBootstrap(nodeId, session);  // step 30
        }
        return;
    }
    if (*command != S2::KeyInstall::NETWORK_KEY_GET)
    {
        return;
    }
    const auto requestedBit = S2::KeyInstall::decodeKeyGet(innerSpan);
    if (!requestedBit.has_value())
    {
        return;
    }
    if ((*requestedBit & session.grantedKeys) == 0)
    {
        // Step 21 (A4): the node asked for a key we didn't grant.
        Logger::warn("[s2-bootstrap] node " + std::to_string(nodeId) + " requested an ungranted key — KEX_FAIL");
        const auto fail = S2::Kex::encodeFail(S2::Kex::FAIL_KEY_GET);
        sendEncrypted(nodeId, std::span<const std::uint8_t>(fail));
        state().sessions.erase(nodeId);
        return;
    }
    const auto classKey = classKeyForBit(*requestedBit);
    if (!classKey.has_value())
    {
        Logger::warn("[s2-bootstrap] node " + std::to_string(nodeId) + " no class key available to transfer");
        return;
    }
    // Step 22: transfer the requested key over the temp channel.
    const auto report = S2::KeyInstall::encodeKeyReport(*requestedBit, *classKey);
    sendEncrypted(nodeId, std::span<const std::uint8_t>(report));
    session.installingKeyBit    = *requestedBit;
    session.installingClassKeys = S2::KeyDerivation::networkKeyExpand(*classKey);
    session.phase               = Phase::AwaitClassNonceGet;
}

// Step 27-29: the node proves it installed the key (NETWORK_KEY_VERIFY, encrypted
// under the *new* class key + its fresh SPAN); on a clean decrypt we answer with
// TRANSFER_END(Key Verified) over the temp channel.
auto onKeyVerify(std::uint8_t nodeId, Session& session, std::span<const std::uint8_t> frame) -> void
{
    const auto nonce = state().classSpanManager.receiveNonce(nodeId, frame);
    if (!nonce.has_value())
    {
        Logger::warn("[s2-bootstrap] node " + std::to_string(nodeId) + " class-channel nonce unavailable");
        return;
    }
    const S2::Encapsulation::Context context{.senderNodeId   = nodeId,
                                             .receiverNodeId = state().controllerNodeId,
                                             .homeId         = state().homeId,
                                             .sequenceNumber = frame[ENCAP_SEQ_OFFSET]};
    const auto inner = S2::Encapsulation::decrypt(frame, context, session.installingClassKeys.keyCcm, *nonce);
    if (!inner.has_value())
    {
        // Step 28 (A5): can't decrypt with the new key — the install failed.
        Logger::warn("[s2-bootstrap] node " + std::to_string(nodeId) + " NETWORK_KEY_VERIFY failed — KEX_FAIL");
        const auto fail = S2::Kex::encodeFail(S2::Kex::FAIL_KEY_VERIFY);
        sendEncrypted(nodeId, std::span<const std::uint8_t>(fail));
        state().sessions.erase(nodeId);
        return;
    }
    Logger::info("[s2-bootstrap] node " + std::to_string(nodeId) + " key class verified");
    sendEncrypted(nodeId, S2::KeyInstall::encodeTransferEnd(true, false));  // step 29
    session.phase = Phase::AwaitNetworkKeyGet;  // node requests the next key or sends the final TRANSFER_END
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
        if (*command == MESSAGE_ENCAPSULATION)
        {
            onTempChannelKeyExchange(event.sourceNodeId, session->second, ccData);
        }
        break;
    case Phase::AwaitClassNonceGet:
        if (*command == S2::NonceSync::NONCE_GET)
        {
            // Step 25-26: fresh SPAN for the new class key. Reset the channel so a
            // stale generator from a prior key can't leak in.
            state().classSpanManager = S2::SpanManager{};
            state().classSpanManager.configurePeer(
                event.sourceNodeId,
                S2::SpanManager::PeerConfig{.classKey        = session->second.installingClassKeys.keyCcm,
                                            .personalization = session->second.installingClassKeys.personalization,
                                            .homeId          = state().homeId,
                                            .ourNodeId       = state().controllerNodeId,
                                            .peerNodeId      = event.sourceNodeId});
            sendPlaintext(event.sourceNodeId, state().classSpanManager.respondToNonceGet(event.sourceNodeId));
            session->second.phase = Phase::AwaitKeyVerify;
        }
        break;
    case Phase::AwaitKeyVerify:
        if (*command == MESSAGE_ENCAPSULATION)
        {
            onKeyVerify(event.sourceNodeId, session->second, ccData);
        }
        break;
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
