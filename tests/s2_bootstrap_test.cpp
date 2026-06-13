// Bus-driven test for the SecurityS2BootstrapOrchestrator (#187): drive the
// plaintext key-agreement phase (KEX_GET -> KEX_REPORT -> KEX_SET ->
// PUBLIC_KEY_REPORT exchange -> ECDH -> temp-key derivation) and assert the
// emitted command sequence, the grant policy, and the completion seam.

#include "Crypto.hpp"  // S2::Crypto::generateKeyPair
#include "Encapsulation.hpp"
#include "Kex.hpp"  // S2::Kex::encode*/decode*
#include "KeyDerivation.hpp"
#include "MessageBus.hpp"
#include "NonceSync.hpp"
#include "PublicKey.hpp"  // S2::PublicKey::encode
#include "Span.hpp"
#include "SpanManager.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CONTROLLER = 1;
constexpr std::uint8_t NODE       = 12;
constexpr std::uint8_t CC_S2      = 0x9F;
const std::array<std::uint8_t, 4> HOME{0xDE, 0xAD, 0xBE, 0xEF};

// A KEX_REPORT advertising scheme 1 + Curve25519 and requesting `keys`.
auto kexReport(std::uint8_t keys) -> std::vector<std::uint8_t>
{
    return S2::Kex::encodeReport(S2::Kex::Report{.echo             = false,
                                                 .requestCsa       = false,
                                                 .supportedSchemes = S2::Kex::KEX_SCHEME_1,
                                                 .supportedCurves  = S2::Kex::ECDH_CURVE25519,
                                                 .requestedKeys    = keys});
}

struct Harness
{
    std::vector<MessageBus::SendDataCommand> sent;
    std::optional<MessageBus::S2KeyAgreementComplete> agreed;
    std::vector<MessageBus::DSKPendingConfirmation> dskPrompts;
    MessageBus::SubscriptionGuard sentGuard;
    MessageBus::SubscriptionGuard agreedGuard;
    MessageBus::SubscriptionGuard dskGuard;

    Harness()
    {
        MessageBus::publish(MessageBus::DongleInfo{.homeId = std::vector<std::uint8_t>(HOME.begin(), HOME.end()),
                                                   .controllerNodeId = CONTROLLER});
        MessageBus::publish(MessageBus::S2NetworkKeysReady{.ready = true});
        sentGuard   = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
            [this](const MessageBus::SendDataCommand& cmd) -> void { sent.push_back(cmd); }));
        agreedGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::S2KeyAgreementComplete>(
            [this](const MessageBus::S2KeyAgreementComplete& event) -> void { agreed = event; }));
        dskGuard    = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DSKPendingConfirmation>(
            [this](const MessageBus::DSKPendingConfirmation& event) -> void { dskPrompts.push_back(event); }));
    }

    // Drive a fresh session up to the DSK-pending park: inclusion, KEX_REPORT
    // for `keys`, then the node's DSK-obfuscated PUBLIC_KEY_REPORT.
    void driveToDskPending(std::uint8_t keys, const S2::Crypto::PublicKey& nodePublicKey)
    {
        MessageBus::publish(MessageBus::NodeIncluded{.nodeId = NODE, .commandClasses = {CC_S2}});
        MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = kexReport(keys)});
        MessageBus::publish(MessageBus::ApplicationCommand{
            .sourceNodeId = NODE, .ccData = S2::PublicKey::encode(false, nodePublicKey, S2::PublicKey::OBFUSCATE_DSK)});
    }
};

// Stand in for the joining node's crypto side: derive the temp keys from the
// controller's emitted public key and run a SpanManager configured to match.
struct NodeSide
{
    S2::SpanManager manager;
    S2::Crypto::Key tempKeyCcm{};
};

auto nodeSideTempChannel(const S2::Crypto::KeyPair& nodeKeys, const S2::Crypto::PublicKey& controllerPub) -> NodeSide
{
    const auto shared = S2::Crypto::ecdh(nodeKeys.privateKey, controllerPub);
    EXPECT_TRUE(shared.has_value());
    const auto temp = S2::KeyDerivation::deriveTempKeys(*shared, controllerPub, nodeKeys.publicKey);
    NodeSide side{.manager = S2::SpanManager{}, .tempKeyCcm = temp.keyCcm};
    side.manager.configurePeer(CONTROLLER,
                               S2::SpanManager::PeerConfig{.classKey        = temp.keyCcm,
                                                           .personalization = temp.personalization,
                                                           .homeId          = HOME,
                                                           .ourNodeId       = NODE,
                                                           .peerNodeId      = CONTROLLER});
    return side;
}

auto receiverContext(std::uint8_t sender,
                     std::uint8_t receiver,
                     std::span<const std::uint8_t> frame) -> S2::Encapsulation::Context
{
    return S2::Encapsulation::Context{
        .senderNodeId = sender, .receiverNodeId = receiver, .homeId = HOME, .sequenceNumber = frame[2]};
}
}  // namespace

TEST(S2Bootstrap, UnauthenticatedReachesKeyAgreement)
{
    Harness harness;

    // 1. Inclusion of an S2 node → KEX_GET.
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = NODE, .commandClasses = {CC_S2}});
    ASSERT_EQ(harness.sent.size(), 1U);
    EXPECT_EQ(harness.sent[0].nodeId, NODE);
    EXPECT_EQ(harness.sent[0].payload, S2::Kex::encodeGet());

    // 2. KEX_REPORT requesting only Unauthenticated → KEX_SET granting it.
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = kexReport(S2::Kex::KEY_S2_UNAUTHENTICATED)});
    ASSERT_EQ(harness.sent.size(), 2U);
    const auto set = S2::Kex::decodeSet(std::span<const std::uint8_t>(harness.sent[1].payload));
    ASSERT_TRUE(set.has_value());
    EXPECT_FALSE(set->echo);
    EXPECT_EQ(set->selectedScheme, S2::Kex::KEX_SCHEME_1);
    EXPECT_EQ(set->selectedCurve, S2::Kex::ECDH_CURVE25519);
    EXPECT_EQ(set->grantedKeys, S2::Kex::KEY_S2_UNAUTHENTICATED);

    // 3. The node's PUBLIC_KEY_REPORT (un-obfuscated for Unauthenticated) → our
    //    PUBLIC_KEY_REPORT, then key agreement completes.
    const auto nodeKeys = S2::Crypto::generateKeyPair();
    MessageBus::publish(MessageBus::ApplicationCommand{
        .sourceNodeId = NODE,
        .ccData       = S2::PublicKey::encode(false, nodeKeys.publicKey, S2::PublicKey::OBFUSCATE_NONE)});
    ASSERT_EQ(harness.sent.size(), 3U);
    const auto ourReport = S2::PublicKey::decode(std::span<const std::uint8_t>(harness.sent[2].payload));
    ASSERT_TRUE(ourReport.has_value());
    EXPECT_TRUE(ourReport->includingNode);  // we are the including controller

    ASSERT_TRUE(harness.agreed.has_value());
    EXPECT_EQ(harness.agreed->nodeId, NODE);
}

TEST(S2Bootstrap, AuthenticatedGrantParksOnDsk)
{
    Harness harness;
    const auto nodeKeys = S2::Crypto::generateKeyPair();

    // The node's PUBLIC_KEY_REPORT (DSK-obfuscated) → we answer, but key
    // agreement does NOT complete; a DSK prompt is raised instead.
    harness.driveToDskPending(S2::Kex::KEY_S2_AUTHENTICATED, nodeKeys.publicKey);
    ASSERT_EQ(harness.sent.size(), 3U);  // KEX_GET, KEX_SET, our PUBLIC_KEY_REPORT
    EXPECT_FALSE(harness.agreed.has_value());
    ASSERT_FALSE(harness.dskPrompts.empty());
    EXPECT_EQ(harness.dskPrompts.back().nodeId, NODE);
    EXPECT_FALSE(harness.dskPrompts.back().dsk.empty());
}

TEST(S2Bootstrap, DskConfirmationCompletesKeyAgreement)
{
    Harness harness;
    const auto nodeKeys = S2::Crypto::generateKeyPair();
    harness.driveToDskPending(S2::Kex::KEY_S2_AUTHENTICATED, nodeKeys.publicKey);
    ASSERT_FALSE(harness.agreed.has_value());

    // Operator supplies the PIN (the first DSK group of the node's real key).
    MessageBus::publish(MessageBus::ConfirmDsk{.nodeId = NODE, .pin = S2::PublicKey::dskPin(nodeKeys.publicKey)});

    ASSERT_TRUE(harness.agreed.has_value());
    EXPECT_EQ(harness.agreed->nodeId, NODE);
    // The pending prompt is retracted (nodeId 0, empty dsk).
    EXPECT_EQ(harness.dskPrompts.back().nodeId, 0);
    EXPECT_TRUE(harness.dskPrompts.back().dsk.empty());
}

TEST(S2Bootstrap, MalformedPinKeepsPromptUp)
{
    Harness harness;
    const auto nodeKeys = S2::Crypto::generateKeyPair();
    harness.driveToDskPending(S2::Kex::KEY_S2_AUTHENTICATED, nodeKeys.publicKey);
    const auto promptsBefore = harness.dskPrompts.size();

    MessageBus::publish(MessageBus::ConfirmDsk{.nodeId = NODE, .pin = "12"});  // not 5 digits
    EXPECT_FALSE(harness.agreed.has_value());
    EXPECT_EQ(harness.dskPrompts.size(), promptsBefore);  // no retract published
}

TEST(S2Bootstrap, NoGrantableKeyFailsKex)
{
    Harness harness;
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = NODE, .commandClasses = {CC_S2}});

    // Requesting only S0 (which this controller doesn't grant over S2) → KEX_FAIL.
    MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = kexReport(S2::Kex::KEY_S0)});
    ASSERT_EQ(harness.sent.size(), 2U);
    const auto failType = S2::Kex::decodeFail(std::span<const std::uint8_t>(harness.sent[1].payload));
    ASSERT_TRUE(failType.has_value());
    EXPECT_EQ(*failType, S2::Kex::FAIL_KEY);
}

TEST(S2Bootstrap, NonS2NodeIsIgnored)
{
    Harness harness;
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = 44, .commandClasses = {0x25, 0x85}});
    EXPECT_TRUE(harness.sent.empty());
}

// Drive the encrypted temp-channel echo phase (steps 13-18): establish the temp
// SPAN via the nonce exchange, send the encrypted KEX_SET echo, and assert the
// controller answers with the matching encrypted KEX_REPORT echo.
TEST(S2Bootstrap, EncryptedChannelVerifiesKexEcho)
{
    Harness harness;
    const auto nodeKeys = S2::Crypto::generateKeyPair();

    // Reach key agreement (Unauthenticated) and learn the controller's public key.
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = NODE, .commandClasses = {CC_S2}});
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = kexReport(S2::Kex::KEY_S2_UNAUTHENTICATED)});
    MessageBus::publish(MessageBus::ApplicationCommand{
        .sourceNodeId = NODE,
        .ccData       = S2::PublicKey::encode(false, nodeKeys.publicKey, S2::PublicKey::OBFUSCATE_NONE)});
    ASSERT_EQ(harness.sent.size(), 3U);  // KEX_GET, KEX_SET, our PUBLIC_KEY_REPORT
    ASSERT_TRUE(harness.agreed.has_value());
    const auto controllerPub = S2::PublicKey::decode(std::span<const std::uint8_t>(harness.sent[2].payload));
    ASSERT_TRUE(controllerPub.has_value());

    auto node = nodeSideTempChannel(nodeKeys, controllerPub->key);

    // Step 13-14: node → NONCE_GET; controller → NONCE_REPORT (plaintext).
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = node.manager.nonceGet(CONTROLLER)});
    ASSERT_EQ(harness.sent.size(), 4U);
    const auto report = S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(harness.sent[3].payload));
    ASSERT_TRUE(report.has_value());
    node.manager.acceptNonceReport(CONTROLLER, *report);

    // Step 16: node → encrypted KEX_SET echo (rides a SPAN extension).
    const auto echoSet  = S2::Kex::encodeSet(S2::Kex::Set{.echo           = true,
                                                          .requestCsa     = false,
                                                          .selectedScheme = S2::Kex::KEX_SCHEME_1,
                                                          .selectedCurve  = S2::Kex::ECDH_CURVE25519,
                                                          .grantedKeys    = S2::Kex::KEY_S2_UNAUTHENTICATED});
    const auto setFrame = node.manager.encrypt(CONTROLLER, std::span<const std::uint8_t>(echoSet));
    ASSERT_TRUE(setFrame.has_value());
    MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = *setFrame});

    // Step 18: controller → encrypted KEX_REPORT echo. Decrypt it node-side.
    ASSERT_EQ(harness.sent.size(), 5U);
    const auto& replyFrame = harness.sent[4].payload;
    const auto nonce       = node.manager.receiveNonce(CONTROLLER, std::span<const std::uint8_t>(replyFrame));
    ASSERT_TRUE(nonce.has_value());
    const auto inner = S2::Encapsulation::decrypt(std::span<const std::uint8_t>(replyFrame),
                                                  receiverContext(CONTROLLER, NODE, replyFrame),
                                                  node.tempKeyCcm,
                                                  *nonce);
    ASSERT_TRUE(inner.has_value());
    const auto reportEcho = S2::Kex::decodeReport(std::span<const std::uint8_t>(*inner));
    ASSERT_TRUE(reportEcho.has_value());
    EXPECT_TRUE(reportEcho->echo);
    EXPECT_EQ(reportEcho->requestedKeys, S2::Kex::KEY_S2_UNAUTHENTICATED);
}
