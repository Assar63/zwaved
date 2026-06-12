// Bus-driven test for SecurityOutboundOrchestrator (#175): a SecureSendRequest
// triggers a NONCE_GET, and the node's NONCE_REPORT yields an encrypted
// SendDataCommand that round-trips back to the original payload. Multiple
// queued sends serialise, one fresh nonce per message.

#include "Encapsulation.hpp"  // S0::Encapsulation::decrypt
#include "MessageBus.hpp"
#include "NetworkKey.hpp"  // S0::NetworkKey::setCurrent
#include "Security.hpp"    // Security::encodeNonceReport

#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CONTROLLER = 1;
constexpr std::uint8_t NODE       = 9;
constexpr std::uint8_t CC_SEC     = 0x98;

const S0::Crypto::Key KEY{
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};

auto setup(std::vector<MessageBus::SendDataCommand>& sink) -> MessageBus::SubscriptionGuard
{
    S0::NetworkKey::setCurrent(KEY);
    MessageBus::publish(MessageBus::DongleInfo{.controllerNodeId = CONTROLLER});
    return MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
        [&](const MessageBus::SendDataCommand& cmd) -> void { sink.push_back(cmd); }));
}
}  // namespace

TEST(S0Outbound, EncryptsAfterNonce)
{
    std::vector<MessageBus::SendDataCommand> sent;
    auto guard = setup(sent);

    const std::vector<std::uint8_t> inner{0x25, 0x01, 0xFF};  // Binary Switch SET on
    constexpr std::uint8_t callback = 0x42;
    MessageBus::publish(MessageBus::SecureSendRequest{.nodeId = NODE, .payload = inner, .callbackId = callback});

    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].nodeId, NODE);
    EXPECT_EQ(sent[0].payload, (std::vector<std::uint8_t>{CC_SEC, 0x40}));  // NONCE_GET

    const S0::Nonce nodeNonce{0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58};
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = Security::encodeNonceReport(nodeNonce)});

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[1].nodeId, NODE);
    EXPECT_EQ(sent[1].callbackId, callback);  // preserved through encapsulation
    EXPECT_EQ(sent[1].payload[1], 0x81);      // MESSAGE_ENCAPSULATION
    const auto decoded =
        S0::Encapsulation::decrypt(std::span<const std::uint8_t>(sent[1].payload), CONTROLLER, NODE, nodeNonce, KEY);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, inner);
}

TEST(S0Outbound, SerialisesQueuedSendsOneNoncePerMessage)
{
    std::vector<MessageBus::SendDataCommand> sent;
    auto guard = setup(sent);

    const std::vector<std::uint8_t> first{0x25, 0x01, 0xFF};
    const std::vector<std::uint8_t> second{0x25, 0x01, 0x00};
    MessageBus::publish(MessageBus::SecureSendRequest{.nodeId = NODE, .payload = first, .callbackId = 1});
    MessageBus::publish(MessageBus::SecureSendRequest{.nodeId = NODE, .payload = second, .callbackId = 2});

    // Only one NONCE_GET so far — the second request is queued behind the first.
    ASSERT_EQ(sent.size(), 1U);

    const S0::Nonce nonce1{0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68};
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = Security::encodeNonceReport(nonce1)});
    // Encrypted first payload + a fresh NONCE_GET for the second.
    ASSERT_EQ(sent.size(), 3U);
    EXPECT_EQ(sent[2].payload, (std::vector<std::uint8_t>{CC_SEC, 0x40}));

    const S0::Nonce nonce2{0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78};
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = Security::encodeNonceReport(nonce2)});
    ASSERT_EQ(sent.size(), 4U);

    const auto firstDecoded =
        S0::Encapsulation::decrypt(std::span<const std::uint8_t>(sent[1].payload), CONTROLLER, NODE, nonce1, KEY);
    const auto secondDecoded =
        S0::Encapsulation::decrypt(std::span<const std::uint8_t>(sent[3].payload), CONTROLLER, NODE, nonce2, KEY);
    ASSERT_TRUE(firstDecoded.has_value());
    ASSERT_TRUE(secondDecoded.has_value());
    EXPECT_EQ(*firstDecoded, first);
    EXPECT_EQ(*secondDecoded, second);
}
