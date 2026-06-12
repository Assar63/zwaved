// Bus-driven test for the SecurityBootstrapOrchestrator (#167): drive the
// full SCHEME_GET -> NONCE_GET -> NETWORK_KEY_SET -> NETWORK_KEY_VERIFY
// sequence and assert the emitted commands (including the temp-key-encrypted
// key set) and the final NodeSecurityStatus.

#include "Encapsulation.hpp"  // S0::Encapsulation::decrypt
#include "MessageBus.hpp"
#include "NetworkKey.hpp"  // S0::NetworkKey::setCurrent
#include "Security.hpp"    // Security::encodeNonceReport

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CONTROLLER = 1;
constexpr std::uint8_t NODE       = 11;
constexpr std::uint8_t CC_SEC     = 0x98;

const S0::Crypto::Key KEY{
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F};
const S0::Nonce NODE_NONCE{0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78};
}  // namespace

TEST(S0Bootstrap, FullSequenceMarksSecure)
{
    const auto dir = std::filesystem::temp_directory_path() / "zwaved_s0_bootstrap_test";
    std::filesystem::create_directories(dir);
    MessageBus::publish(MessageBus::StorageConfig{.stateDir = dir.string()});
    S0::NetworkKey::setCurrent(KEY);
    MessageBus::publish(MessageBus::DongleInfo{.controllerNodeId = CONTROLLER});

    std::vector<MessageBus::SendDataCommand> sent;
    std::optional<MessageBus::NodeSecurityStatus> status;
    auto sentGuard   = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
        [&](const MessageBus::SendDataCommand& cmd) -> void { sent.push_back(cmd); }));
    auto statusGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeSecurityStatus>(
        [&](const MessageBus::NodeSecurityStatus& event) -> void { status = event; }));

    // 1. Inclusion of a node advertising CC 0x98 → SCHEME_GET.
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = NODE, .commandClasses = {CC_SEC}});
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].nodeId, NODE);
    EXPECT_EQ(sent[0].payload, (std::vector<std::uint8_t>{CC_SEC, 0x04, 0x00}));  // SCHEME_GET

    // 2. SCHEME_REPORT → NONCE_GET.
    MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = {CC_SEC, 0x05, 0x00}});
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[1].payload, (std::vector<std::uint8_t>{CC_SEC, 0x40}));  // NONCE_GET

    // 3. NONCE_REPORT → encrypted NETWORK_KEY_SET (temp key + node's nonce).
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = Security::encodeNonceReport(NODE_NONCE)});
    ASSERT_EQ(sent.size(), 3U);
    EXPECT_EQ(sent[2].payload[0], CC_SEC);
    EXPECT_EQ(sent[2].payload[1], 0x81);  // MESSAGE_ENCAPSULATION
    const S0::Crypto::Key tempKey{};      // all-zero temporary key
    const auto inner = S0::Encapsulation::decrypt(
        std::span<const std::uint8_t>(sent[2].payload), CONTROLLER, NODE, NODE_NONCE, tempKey);
    ASSERT_TRUE(inner.has_value());
    std::vector<std::uint8_t> expected{CC_SEC, 0x06};  // NETWORK_KEY_SET + the real key
    expected.insert(expected.end(), KEY.begin(), KEY.end());
    EXPECT_EQ(*inner, expected);

    // 4. NETWORK_KEY_VERIFY (the seam would have decrypted it) → secure.
    EXPECT_FALSE(status.has_value());
    MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = {CC_SEC, 0x07}});
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->nodeId, NODE);
    EXPECT_TRUE(status->secure);
}

TEST(S0Bootstrap, NonSecureNodeIsIgnored)
{
    S0::NetworkKey::setCurrent(KEY);
    std::vector<MessageBus::SendDataCommand> sent;
    auto sentGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
        [&](const MessageBus::SendDataCommand& cmd) -> void { sent.push_back(cmd); }));

    // A node whose NIF does not list CC 0x98 — no bootstrap traffic.
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = 42, .commandClasses = {0x25, 0x85}});
    EXPECT_TRUE(sent.empty());
}
