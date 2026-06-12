// Bus-driven test for the constructor-armed NonceResponder: an inbound
// SECURITY_NONCE_GET should produce exactly one SECURITY_NONCE_REPORT
// SendDataCommand back to the asking node; anything else is ignored.

#include "MessageBus.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_SECURITY = 0x98;
constexpr std::uint8_t CMD_GET     = 0x40;
constexpr std::uint8_t CMD_REPORT  = 0x80;
constexpr std::uint8_t PEER        = 5;
}  // namespace

TEST(S0NonceResponder, RepliesToNonceGet)
{
    std::optional<MessageBus::SendDataCommand> captured;
    auto guard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
        [&](const MessageBus::SendDataCommand& cmd) -> void { captured = cmd; }));

    MessageBus::publish(
        MessageBus::ApplicationCommand{.rxStatus = 0, .sourceNodeId = PEER, .ccData = {CC_SECURITY, CMD_GET}});

    ASSERT_TRUE(captured.has_value());
    EXPECT_EQ(captured->nodeId, PEER);
    ASSERT_EQ(captured->payload.size(), 10U);  // CC + cmd + 8-byte nonce
    EXPECT_EQ(captured->payload[0], CC_SECURITY);
    EXPECT_EQ(captured->payload[1], CMD_REPORT);
}

TEST(S0NonceResponder, IgnoresNonNonceGetFrames)
{
    std::optional<MessageBus::SendDataCommand> captured;
    auto guard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SendDataCommand>(
        [&](const MessageBus::SendDataCommand& cmd) -> void { captured = cmd; }));

    // A NONCE_REPORT (not a GET) and a non-Security frame must both be ignored.
    MessageBus::publish(MessageBus::ApplicationCommand{
        .rxStatus = 0, .sourceNodeId = PEER, .ccData = {CC_SECURITY, CMD_REPORT, 1, 2, 3, 4, 5, 6, 7, 8}});
    MessageBus::publish(
        MessageBus::ApplicationCommand{.rxStatus = 0, .sourceNodeId = PEER, .ccData = {0x25, 0x01, 0xFF}});

    EXPECT_FALSE(captured.has_value());
}
