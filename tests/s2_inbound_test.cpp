// Bus-driven test for the SecurityS2InboundOrchestrator (#199): a node already
// bootstrapped to an S2 class sends an encrypted command; the seam establishes
// the SPAN from the nonce exchange, decrypts, and republishes the plaintext CC.

#include "Crypto.hpp"
#include "Encapsulation.hpp"
#include "KeyDerivation.hpp"
#include "MessageBus.hpp"
#include "NetworkKeys.hpp"
#include "NodeRegistry.hpp"
#include "NonceSync.hpp"
#include "SpanManager.hpp"
#include "Transport.hpp"

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
constexpr std::uint8_t NODE       = 7;
const std::array<std::uint8_t, 4> HOME{0x01, 0x02, 0x03, 0x04};

// A node-side SpanManager configured with the same class keys as the daemon.
auto nodeSide(const S2::Crypto::Key& classKey) -> S2::SpanManager
{
    const auto derived = S2::KeyDerivation::networkKeyExpand(classKey);
    S2::SpanManager node;
    node.configurePeer(CONTROLLER,
                       S2::SpanManager::PeerConfig{.classKey        = derived.keyCcm,
                                                   .personalization = derived.personalization,
                                                   .homeId          = HOME,
                                                   .ourNodeId       = NODE,
                                                   .peerNodeId      = CONTROLLER});
    return node;
}
}  // namespace

TEST(S2Inbound, DecryptsSecureCommandFromNode)
{
    const auto dir = std::filesystem::temp_directory_path() / "zwaved_s2_inbound_test";
    std::filesystem::create_directories(dir);
    MessageBus::publish(MessageBus::StorageConfig{.stateDir = dir.string()});

    // A known Unauthenticated class key; register NODE and mark it secure at
    // that class (setSecurityScheme no-ops for an unknown node).
    S2::NetworkKeys::KeySet keys{};
    keys.at(static_cast<std::size_t>(S2::NetworkKeys::Class::Unauthenticated)).fill(0x5C);
    S2::NetworkKeys::setCurrent(keys);
    NodeRegistry::setHomeId(std::vector<std::uint8_t>(HOME.begin(), HOME.end()));  // add() needs a bound home
    NodeRegistry::add(NodeRegistry::NodeInfo{.nodeId = NODE});
    NodeRegistry::setSecurityScheme(NODE, NodeRegistry::SecurityScheme::S2Unauthenticated);

    MessageBus::publish(MessageBus::DongleInfo{.homeId           = std::vector<std::uint8_t>(HOME.begin(), HOME.end()),
                                               .controllerNodeId = CONTROLLER});

    std::vector<MessageBus::SendDataCommand> sent;
    std::vector<MessageBus::ApplicationCommand> appCmds;
    auto sentGuard   = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
        [&](const MessageBus::SendDataCommand& cmd) -> void { sent.push_back(cmd); }));
    auto appCmdGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(
        [&](const MessageBus::ApplicationCommand& cmd) -> void { appCmds.push_back(cmd); }));

    auto node = nodeSide(keys.at(static_cast<std::size_t>(S2::NetworkKeys::Class::Unauthenticated)));

    // 1. Node → NONCE_GET; the seam answers with a NONCE_REPORT (its REI).
    MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = node.nonceGet(CONTROLLER)});
    ASSERT_FALSE(sent.empty());
    const auto report = S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(sent.back().payload));
    ASSERT_TRUE(report.has_value());
    node.acceptNonceReport(CONTROLLER, *report);

    // 2. Node → encrypted BinarySwitch Report; the seam decrypts + republishes it.
    const std::vector<std::uint8_t> plaintext{0x25, 0x03, 0xFF};  // BinarySwitch Report = on
    const auto frame = node.encrypt(CONTROLLER, std::span<const std::uint8_t>(plaintext));
    ASSERT_TRUE(frame.has_value());
    MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = NODE, .ccData = *frame});

    // The decrypted inner CC was republished as a fresh ApplicationCommand.
    bool sawPlaintext = false;
    for (const auto& cmd : appCmds)
    {
        if (cmd.sourceNodeId == NODE && cmd.ccData == plaintext)
        {
            sawPlaintext = true;
        }
    }
    EXPECT_TRUE(sawPlaintext);
}

TEST(S2Inbound, IgnoresNonSecureNode)
{
    MessageBus::publish(MessageBus::DongleInfo{.homeId           = std::vector<std::uint8_t>(HOME.begin(), HOME.end()),
                                               .controllerNodeId = CONTROLLER});
    std::vector<MessageBus::SendDataCommand> sent;
    auto sentGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
        [&](const MessageBus::SendDataCommand& cmd) -> void { sent.push_back(cmd); }));

    // A non-secure node (id 99) sends a 0x9F NONCE_GET — the seam must not answer.
    MessageBus::publish(
        MessageBus::ApplicationCommand{.sourceNodeId = 99, .ccData = S2::NonceSync::encodeNonceGet(0x01)});
    EXPECT_TRUE(sent.empty());
}
