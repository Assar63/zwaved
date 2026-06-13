#include "SpanManager.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <openssl/rand.h>

namespace
{
constexpr std::uint8_t MESSAGE_ENCAPSULATION   = 0x03;                  // S2 command byte for the encrypted wrapper
constexpr std::uint8_t PROPS_NON_ENCRYPTED_EXT = 0x01;                  // props bit0 — unencrypted extensions present
constexpr std::size_t ENCAP_HEADER_SIZE        = 4;                     // CC + command + seq + props
constexpr std::size_t SPAN_NONCE_SIZE          = S2::SPAN::NONCE_SIZE;  // 16
constexpr std::size_t CCM_NONCE_SIZE           = S2::Encapsulation::CCM_NONCE_SIZE;  // 13
}  // namespace

auto S2::SpanManager::randomEntropyInput() -> SPAN::EntropyInput
{
    SPAN::EntropyInput entropy{};
    if (RAND_bytes(entropy.data(), static_cast<int>(entropy.size())) != 1)
    {
        throw std::runtime_error("S2::SpanManager: OpenSSL RAND_bytes failed");
    }
    return entropy;
}

S2::SpanManager::SpanManager(EntropySource entropy)
    : entropy_(std::move(entropy))
{
}

auto S2::SpanManager::peerState(std::uint8_t peer) -> Peer&
{
    return peers_[peer];
}

auto S2::SpanManager::configurePeer(std::uint8_t peer, const PeerConfig& config) -> void
{
    peerState(peer).config = config;
}

auto S2::SpanManager::hasSpan(std::uint8_t peer) const -> bool
{
    const auto found = peers_.find(peer);
    return found != peers_.end() && found->second.span.has_value();
}

auto S2::SpanManager::truncatedNonce(SPAN::Span& span) -> Encapsulation::CcmNonce
{
    const SPAN::Nonce full = span.nextNonce();  // 16 bytes
    static_assert(SPAN_NONCE_SIZE >= CCM_NONCE_SIZE);
    Encapsulation::CcmNonce nonce{};
    std::copy_n(full.begin(), CCM_NONCE_SIZE, nonce.begin());  // most-significant 13 bytes
    return nonce;
}

auto S2::SpanManager::nonceGet(std::uint8_t peer) -> std::vector<std::uint8_t>
{
    auto& state = peerState(peer);
    return NonceSync::encodeNonceGet(state.txSequence++);
}

auto S2::SpanManager::respondToNonceGet(std::uint8_t peer) -> std::vector<std::uint8_t>
{
    auto& state    = peerState(peer);
    const auto rei = entropy_();
    state.ourRei   = rei;
    return NonceSync::encodeNonceReport(
        NonceSync::Report{.sequenceNumber = state.txSequence++, .sos = true, .mos = false, .rei = rei});
}

auto S2::SpanManager::acceptNonceReport(std::uint8_t peer, const NonceSync::Report& report) -> void
{
    if (report.sos && report.rei.has_value())
    {
        peerState(peer).peerRei = report.rei;
    }
}

auto S2::SpanManager::encrypt(std::uint8_t peer,
                              std::span<const std::uint8_t> inner) -> std::optional<std::vector<std::uint8_t>>
{
    auto& state = peerState(peer);

    std::vector<std::uint8_t> spanExtension;
    if (!state.span.has_value())
    {
        if (!state.peerRei.has_value())
        {
            return std::nullopt;  // need a NONCE_GET round trip first
        }
        const auto sei = entropy_();
        state.span     = SPAN::Span::instantiate(sei, *state.peerRei, state.config.personalization);
        state.peerRei.reset();
        spanExtension = NonceSync::encodeSpanExtension(sei);
    }

    const Encapsulation::Context context{.senderNodeId   = state.config.ourNodeId,
                                         .receiverNodeId = state.config.peerNodeId,
                                         .homeId         = state.config.homeId,
                                         .sequenceNumber = state.txSequence++};
    const auto nonce = truncatedNonce(*state.span);
    return Encapsulation::encrypt(
        inner, context, state.config.classKey, nonce, std::span<const std::uint8_t>(spanExtension));
}

auto S2::SpanManager::receiveNonce(std::uint8_t peer,
                                   std::span<const std::uint8_t> frame) -> std::optional<Encapsulation::CcmNonce>
{
    auto& state = peerState(peer);

    const bool isEncap =
        frame.size() >= ENCAP_HEADER_SIZE && frame[0] == NonceSync::COMMAND_CLASS && frame[1] == MESSAGE_ENCAPSULATION;
    if (isEncap && (frame[3] & PROPS_NON_ENCRYPTED_EXT) != 0 && state.ourRei.has_value())
    {
        const auto sei = NonceSync::findSpanExtension(frame.subspan(ENCAP_HEADER_SIZE));
        if (sei.has_value())
        {
            state.span = SPAN::Span::instantiate(*sei, *state.ourRei, state.config.personalization);
            state.ourRei.reset();
        }
    }

    if (!state.span.has_value())
    {
        return std::nullopt;  // can't derive a nonce — caller should respondToNonceGet()
    }
    return truncatedNonce(*state.span);
}
