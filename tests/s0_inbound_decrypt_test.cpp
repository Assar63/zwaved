// Integration test for the inbound S0 decrypt seam (cc-translator's
// Encapsulation.cpp, #166): a MESSAGE_ENCAPSULATION frame addressed with a
// nonce we issued should be authenticated, decrypted, and the inner CC frame
// republished as a fresh ApplicationCommand, with NodeSecurityStatus emitted.

#include "Encapsulation.hpp"  // S0::Encapsulation::encrypt
#include "MessageBus.hpp"
#include "NetworkKey.hpp"  // S0::NetworkKey::setCurrent
#include "NonceTable.hpp"  // S0::issuedNonces

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CONTROLLER  = 1;
constexpr std::uint8_t CC_SECURITY = 0x98;

const S0::Crypto::Key KEY{
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
const S0::Nonce SENDER_NONCE{0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97};
const std::vector<std::uint8_t> INNER{0x25, 0x01, 0xFF};  // Binary Switch SET on
}  // namespace

TEST(S0InboundDecrypt, AuthenticatesDecryptsAndRepublishes)
{
    constexpr std::uint8_t peer = 11;
    S0::NetworkKey::setCurrent(KEY);
    MessageBus::publish(MessageBus::DongleInfo{.controllerNodeId = CONTROLLER});

    // The nonce *we* issued to the peer (what it uses as the receiver nonce).
    const auto ourNonce = S0::issuedNonces().generate(peer, S0::NonceTable::Clock::now());
    const auto frame =
        S0::Encapsulation::encrypt(std::span<const std::uint8_t>(INNER), peer, CONTROLLER, SENDER_NONCE, ourNonce, KEY);

    std::optional<std::vector<std::uint8_t>> innerSeen;
    std::optional<MessageBus::NodeSecurityStatus> statusSeen;
    auto cmdGuard    = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(
        [&](const MessageBus::ApplicationCommand& event) -> void
        {
            if (!event.ccData.empty() && event.ccData[0] != CC_SECURITY)
            {
                innerSeen = event.ccData;  // the republished plaintext inner frame
            }
        }));
    auto statusGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeSecurityStatus>(
        [&](const MessageBus::NodeSecurityStatus& status) -> void { statusSeen = status; }));

    MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = peer, .ccData = frame});

    ASSERT_TRUE(innerSeen.has_value());
    EXPECT_EQ(*innerSeen, INNER);
    ASSERT_TRUE(statusSeen.has_value());
    EXPECT_EQ(statusSeen->nodeId, peer);
    EXPECT_TRUE(statusSeen->secure);
}

TEST(S0InboundDecrypt, UnknownNonceIsDropped)
{
    constexpr std::uint8_t peer = 12;  // distinct peer — no nonce ever issued to it
    S0::NetworkKey::setCurrent(KEY);
    MessageBus::publish(MessageBus::DongleInfo{.controllerNodeId = CONTROLLER});

    const S0::Nonce neverIssued{0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7};
    const auto frame = S0::Encapsulation::encrypt(
        std::span<const std::uint8_t>(INNER), peer, CONTROLLER, SENDER_NONCE, neverIssued, KEY);

    bool innerSeen   = false;
    bool statusSeen  = false;
    auto cmdGuard    = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(
        [&](const MessageBus::ApplicationCommand& event) -> void
        {
            if (!event.ccData.empty() && event.ccData[0] != CC_SECURITY)
            {
                innerSeen = true;
            }
        }));
    auto statusGuard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeSecurityStatus>(
        [&](const MessageBus::NodeSecurityStatus&) -> void { statusSeen = true; }));

    MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = peer, .ccData = frame});

    EXPECT_FALSE(innerSeen);
    EXPECT_FALSE(statusSeen);
}
