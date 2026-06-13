// Security S2 (CC 0x9F) SPAN runtime (#187/#199): drive two SpanManagers — a
// controller and a node — through the nonce-sync handshake and assert they
// derive identical SPANs (a frame one encrypts, the other decrypts) and stay in
// lockstep across subsequent frames. Uses a deterministic entropy source so the
// SEIs/REIs are reproducible.

#include "Crypto.hpp"
#include "Encapsulation.hpp"
#include "NonceSync.hpp"
#include "Span.hpp"
#include "SpanManager.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CONTROLLER = 1;
constexpr std::uint8_t NODE       = 9;

const S2::Crypto::Key CLASS_KEY{
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
const S2::SPAN::Personalization PERS{0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
                                     0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
                                     0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
const std::array<std::uint8_t, 4> HOME{0xDE, 0xAD, 0xBE, 0xEF};

// A deterministic entropy source: each call returns a distinct, repeatable EI.
auto counterEntropy(std::uint8_t seed) -> S2::SpanManager::EntropySource
{
    auto counter = std::make_shared<std::uint8_t>(seed);
    return [counter]() -> S2::SPAN::EntropyInput
    {
        S2::SPAN::EntropyInput entropy{};
        entropy.fill(*counter);
        *counter += 1;
        return entropy;
    };
}

auto makeManager(std::uint8_t entropySeed, std::uint8_t ours, std::uint8_t peer) -> S2::SpanManager
{
    S2::SpanManager manager(counterEntropy(entropySeed));
    manager.configurePeer(
        peer,
        S2::SpanManager::PeerConfig{
            .classKey = CLASS_KEY, .personalization = PERS, .homeId = HOME, .ourNodeId = ours, .peerNodeId = peer});
    return manager;
}

// The context the receiver passes to Encapsulation::decrypt: identities mirror
// the sender's, sequence number comes off the frame.
auto receiverContext(std::uint8_t sender,
                     std::uint8_t receiver,
                     std::span<const std::uint8_t> frame) -> S2::Encapsulation::Context
{
    return S2::Encapsulation::Context{
        .senderNodeId = sender, .receiverNodeId = receiver, .homeId = HOME, .sequenceNumber = frame[2]};
}
}  // namespace

TEST(S2SpanManager, HandshakeEstablishesMatchingSpanAndRoundTrips)
{
    auto controller = makeManager(0x01, CONTROLLER, NODE);
    auto node       = makeManager(0x80, NODE, CONTROLLER);

    // Controller wants to send but has no SPAN → it can't encrypt yet.
    const std::vector<std::uint8_t> payload{0x25, 0x01, 0xFF};  // e.g. BinarySwitch Set
    EXPECT_FALSE(controller.encrypt(NODE, std::span<const std::uint8_t>(payload)).has_value());

    // Controller → NONCE_GET; node replies NONCE_REPORT(SOS, REI).
    const auto nonceGet = controller.nonceGet(NODE);
    ASSERT_EQ(S2::NonceSync::commandByte(std::span<const std::uint8_t>(nonceGet)), S2::NonceSync::NONCE_GET);
    const auto nonceReport = node.respondToNonceGet(CONTROLLER);
    const auto report      = S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(nonceReport));
    ASSERT_TRUE(report.has_value());
    controller.acceptNonceReport(NODE, *report);

    // Now the controller can encrypt — it establishes the SPAN and rides its SEI
    // along in a SPAN extension.
    const auto frame = controller.encrypt(NODE, std::span<const std::uint8_t>(payload));
    ASSERT_TRUE(frame.has_value());
    EXPECT_TRUE(controller.hasSpan(NODE));

    // The node derives the identical SPAN from the frame's extension and decrypts.
    const auto nonce = node.receiveNonce(CONTROLLER, std::span<const std::uint8_t>(*frame));
    ASSERT_TRUE(nonce.has_value());
    EXPECT_TRUE(node.hasSpan(CONTROLLER));
    const auto inner = S2::Encapsulation::decrypt(
        std::span<const std::uint8_t>(*frame), receiverContext(CONTROLLER, NODE, *frame), CLASS_KEY, *nonce);
    ASSERT_TRUE(inner.has_value());
    EXPECT_EQ(*inner, payload);

    // A second frame uses the established SPAN (no extension) and stays in lockstep.
    const std::vector<std::uint8_t> payload2{0x25, 0x01, 0x00};
    const auto frame2 = controller.encrypt(NODE, std::span<const std::uint8_t>(payload2));
    ASSERT_TRUE(frame2.has_value());
    const auto nonce2 = node.receiveNonce(CONTROLLER, std::span<const std::uint8_t>(*frame2));
    ASSERT_TRUE(nonce2.has_value());
    const auto inner2 = S2::Encapsulation::decrypt(
        std::span<const std::uint8_t>(*frame2), receiverContext(CONTROLLER, NODE, *frame2), CLASS_KEY, *nonce2);
    ASSERT_TRUE(inner2.has_value());
    EXPECT_EQ(*inner2, payload2);
}

TEST(S2SpanManager, ReceiveWithoutSpanYieldsNoNonce)
{
    auto node = makeManager(0x80, NODE, CONTROLLER);
    // An encap frame arrives but we have no SPAN and never issued a REI → can't
    // derive a nonce; the caller must answer with a NONCE_REPORT.
    const std::vector<std::uint8_t> bogusEncap{0x9F, 0x03, 0x05, 0x00, 0xAA, 0xBB};
    EXPECT_FALSE(node.receiveNonce(CONTROLLER, std::span<const std::uint8_t>(bogusEncap)).has_value());
}

TEST(S2SpanManager, NonceReportWithoutSosIsNotStored)
{
    auto controller = makeManager(0x01, CONTROLLER, NODE);
    const S2::NonceSync::Report noSos{.sequenceNumber = 3, .sos = false, .mos = true, .rei = std::nullopt};
    controller.acceptNonceReport(NODE, noSos);
    // Still no peer REI → still can't encrypt.
    const std::vector<std::uint8_t> payload{0x20, 0x01, 0x10};
    EXPECT_FALSE(controller.encrypt(NODE, std::span<const std::uint8_t>(payload)).has_value());
}
