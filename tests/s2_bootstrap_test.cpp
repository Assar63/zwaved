// Bus-driven test for the SecurityS2BootstrapOrchestrator (#187): drive the
// plaintext key-agreement phase (KEX_GET -> KEX_REPORT -> KEX_SET ->
// PUBLIC_KEY_REPORT exchange -> ECDH -> temp-key derivation) and assert the
// emitted command sequence, the grant policy, and the completion seam.

#include "Crypto.hpp"  // S2::Crypto::generateKeyPair
#include "Kex.hpp"     // S2::Kex::encode*/decode*
#include "MessageBus.hpp"
#include "PublicKey.hpp"  // S2::PublicKey::encode

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
    MessageBus::SubscriptionGuard sentGuard;
    MessageBus::SubscriptionGuard agreedGuard;

    Harness()
    {
        MessageBus::publish(MessageBus::DongleInfo{.controllerNodeId = CONTROLLER});
        MessageBus::publish(MessageBus::S2NetworkKeysReady{.ready = true});
        sentGuard   = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
            [this](const MessageBus::SendDataCommand& cmd) -> void { sent.push_back(cmd); }));
        agreedGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::S2KeyAgreementComplete>(
            [this](const MessageBus::S2KeyAgreementComplete& event) -> void { agreed = event; }));
    }
};
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
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = NODE, .commandClasses = {CC_S2}});
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = kexReport(S2::Kex::KEY_S2_AUTHENTICATED)});

    // The node's PUBLIC_KEY_REPORT (DSK-obfuscated) → we answer, but key
    // agreement does NOT complete (PIN required, deferred to the DSK layer).
    const auto nodeKeys = S2::Crypto::generateKeyPair();
    MessageBus::publish(MessageBus::ApplicationCommand{
        .sourceNodeId = NODE,
        .ccData       = S2::PublicKey::encode(false, nodeKeys.publicKey, S2::PublicKey::OBFUSCATE_DSK)});
    ASSERT_EQ(harness.sent.size(), 3U);  // KEX_GET, KEX_SET, our PUBLIC_KEY_REPORT
    EXPECT_FALSE(harness.agreed.has_value());
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
