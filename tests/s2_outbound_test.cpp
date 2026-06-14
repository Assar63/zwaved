// Bus-driven test for the SecurityS2OutboundOrchestrator (#199): a SecureS2Send
// Request for an S2-secure node triggers a nonce exchange and then an encrypted
// MESSAGE_ENCAPSULATION the node can decrypt back to the original payload.

#include "Crypto.hpp"
#include "Encapsulation.hpp"
#include "KeyDerivation.hpp"
#include "MessageBus.hpp"
#include "NetworkKeys.hpp"
#include "NodeRegistry.hpp"
#include "NonceSync.hpp"
#include "SpanManager.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CONTROLLER = 1;
constexpr std::uint8_t NODE       = 8;
const std::array<std::uint8_t, 4> HOME{0x0A, 0x0B, 0x0C, 0x0D};

auto receiverContext(std::uint8_t sender,
                     std::uint8_t receiver,
                     std::span<const std::uint8_t> frame) -> S2::Encapsulation::Context
{
    return S2::Encapsulation::Context{
        .senderNodeId = sender, .receiverNodeId = receiver, .homeId = HOME, .sequenceNumber = frame[2]};
}
}  // namespace

TEST(S2Outbound, EncryptsSecureCommandToNode)
{
    const auto dir = std::filesystem::temp_directory_path() / "zwaved_s2_outbound_test";
    std::filesystem::create_directories(dir);
    MessageBus::publish(MessageBus::StorageConfig{.stateDir = dir.string()});

    S2::NetworkKeys::KeySet keys{};
    keys.at(static_cast<std::size_t>(S2::NetworkKeys::Class::Unauthenticated)).fill(0x71);
    S2::NetworkKeys::setCurrent(keys);
    NodeRegistry::setHomeId(std::vector<std::uint8_t>(HOME.begin(), HOME.end()));
    NodeRegistry::add(NodeRegistry::NodeInfo{.nodeId = NODE});
    NodeRegistry::setSecurityScheme(NODE, NodeRegistry::SecurityScheme::S2Unauthenticated);

    MessageBus::publish(MessageBus::DongleInfo{.homeId           = std::vector<std::uint8_t>(HOME.begin(), HOME.end()),
                                               .controllerNodeId = CONTROLLER});

    std::vector<MessageBus::SendDataCommand> sent;
    auto sentGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
        [&](const MessageBus::SendDataCommand& cmd) -> void { sent.push_back(cmd); }));

    // The node side, configured with the matching class keys.
    const auto derived =
        S2::KeyDerivation::networkKeyExpand(keys.at(static_cast<std::size_t>(S2::NetworkKeys::Class::Unauthenticated)));
    S2::SpanManager node;
    node.configurePeer(CONTROLLER,
                       S2::SpanManager::PeerConfig{.classKey        = derived.keyCcm,
                                                   .personalization = derived.personalization,
                                                   .homeId          = HOME,
                                                   .ourNodeId       = NODE,
                                                   .peerNodeId      = CONTROLLER});

    // 1. A secure send with no SPAN yet → the orchestrator emits a NONCE_GET.
    const std::vector<std::uint8_t> payload{0x25, 0x01, 0xFF};  // BinarySwitch Set on
    MessageBus::publish(MessageBus::SecureS2SendRequest{.nodeId = NODE, .payload = payload, .callbackId = 0x44});
    ASSERT_EQ(sent.size(), 1U);
    ASSERT_EQ(S2::NonceSync::commandByte(std::span<const std::uint8_t>(sent[0].payload)), S2::NonceSync::NONCE_GET);

    // 2. Node answers the NONCE_GET; the orchestrator encrypts + sends the payload.
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = node.respondToNonceGet(CONTROLLER)});
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[1].nodeId, NODE);
    EXPECT_EQ(sent[1].callbackId, 0x44);  // preserved for SendDataCallback correlation

    // The node decrypts the wrapper back to the original payload.
    const auto& frame = sent[1].payload;
    const auto nonce  = node.receiveNonce(CONTROLLER, std::span<const std::uint8_t>(frame));
    ASSERT_TRUE(nonce.has_value());
    const auto inner = S2::Encapsulation::decrypt(
        std::span<const std::uint8_t>(frame), receiverContext(CONTROLLER, NODE, frame), derived.keyCcm, *nonce);
    ASSERT_TRUE(inner.has_value());
    EXPECT_EQ(*inner, payload);
}

TEST(S2Outbound, NonSecureNodeIsNotHandled)
{
    MessageBus::publish(MessageBus::DongleInfo{.homeId           = std::vector<std::uint8_t>(HOME.begin(), HOME.end()),
                                               .controllerNodeId = CONTROLLER});
    std::vector<MessageBus::SendDataCommand> sent;
    auto sentGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
        [&](const MessageBus::SendDataCommand& cmd) -> void { sent.push_back(cmd); }));

    // Node 55 was never marked secure → the orchestrator drops the request.
    MessageBus::publish(MessageBus::SecureS2SendRequest{.nodeId = 55, .payload = {0x25, 0x01, 0x00}, .callbackId = 0});
    EXPECT_TRUE(sent.empty());
}
