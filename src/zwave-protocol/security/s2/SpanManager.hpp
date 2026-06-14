#ifndef ZWAVED_S2_SPAN_MANAGER_HPP
#define ZWAVED_S2_SPAN_MANAGER_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"         // S2::Crypto::Key
#include "Encapsulation.hpp"  // S2::Encapsulation::CcmNonce / Context
#include "NonceSync.hpp"      // S2::NonceSync::Report
#include "Span.hpp"           // S2::SPAN::Span / EntropyInput / Personalization
// IWYU pragma: end_exports

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

/// Security S2 (CC 0x9F) SPAN runtime — the per-peer nonce-synchronisation
/// state machine that turns the NonceSync wire codec (#187/#199) into a live
/// exchange. It owns each peer's SPAN generator + the in-flight Entropy Inputs +
/// the outgoing sequence counter, and produces the frames / CCM nonces the
/// caller puts on (and takes off) the wire.
///
/// Establishment, singlecast (SDS13783 §4.2.6.4.18 + §4.2.6.5.9-13): the SENDER
/// needs the receiver's REI before it can build a SPAN. The usual flow:
///   send wants to send but has no SPAN -> nonceGet() (NONCE_GET)
///     -> peer replies NONCE_REPORT(SOS, REI), fed to acceptNonceReport()
///     -> encrypt() now mixes a fresh SEI with that REI, instantiates the SPAN,
///        and rides the SEI to the peer in a SPAN extension on the frame.
///   The receiver, having earlier handed out that REI (respondToNonceGet /
///   receiveNonce on a frame it couldn't decrypt), instantiates the identical
///   SPAN from the extension's SEI + its stored REI. Both then draw nonces in
///   lockstep with no further exchange.
///
/// Pure state machine — no threads, no bus, no I/O. The entropy source is
/// injectable for deterministic tests; the default is OpenSSL CSPRNG. The
/// ProtocolThread / orchestrator wiring that pumps real frames through it is the
/// remaining #199 integration.
namespace S2
{
class SpanManager
{
  public:
    using EntropySource = std::function<SPAN::EntropyInput()>;

    /// 16 cryptographically-random bytes from the OpenSSL CSPRNG.
    [[nodiscard]] static auto randomEntropyInput() -> SPAN::EntropyInput;

    explicit SpanManager(EntropySource entropy = randomEntropyInput);

    /// The crypto context for one peer: the class key + personalization the SPAN
    /// is bound to, plus the identity fields the encapsulation AAD authenticates.
    struct PeerConfig
    {
        Crypto::Key classKey{};
        SPAN::Personalization personalization{};
        std::array<std::uint8_t, 4> homeId{};
        std::uint8_t ourNodeId  = 0;
        std::uint8_t peerNodeId = 0;
    };

    auto configurePeer(std::uint8_t peer, const PeerConfig& config) -> void;

    [[nodiscard]] auto hasSpan(std::uint8_t peer) const -> bool;

    /// A NONCE_GET asking the peer for a fresh REI (so a later encrypt() can
    /// establish the SPAN). Advances the peer's sequence number.
    [[nodiscard]] auto nonceGet(std::uint8_t peer) -> std::vector<std::uint8_t>;

    /// Reply to an inbound NONCE_GET (or an undecryptable frame): a NONCE_REPORT
    /// with the SOS flag and a fresh REI, which we stash until the peer's SPAN
    /// extension arrives.
    [[nodiscard]] auto respondToNonceGet(std::uint8_t peer) -> std::vector<std::uint8_t>;

    /// Accept an inbound NONCE_REPORT; when SOS is set its REI is stored so the
    /// next encrypt() can establish the SPAN.
    auto acceptNonceReport(std::uint8_t peer, const NonceSync::Report& report) -> void;

    /// Encrypt `inner` for the peer. If a SPAN is established it's used directly;
    /// otherwise, if the peer's REI is on hand, a SPAN is instantiated and a SPAN
    /// extension carrying our SEI is attached. Returns std::nullopt when neither
    /// is available — the caller must run a NONCE_GET round trip first.
    [[nodiscard]] auto encrypt(std::uint8_t peer,
                               std::span<const std::uint8_t> inner) -> std::optional<std::vector<std::uint8_t>>;

    /// The CCM nonce for decrypting an inbound MESSAGE_ENCAPSULATION `frame`,
    /// instantiating the SPAN from the frame's SPAN extension if present.
    /// std::nullopt if we can't yet (no SPAN and no extension) — the caller
    /// should answer with respondToNonceGet().
    [[nodiscard]] auto receiveNonce(std::uint8_t peer,
                                    std::span<const std::uint8_t> frame) -> std::optional<Encapsulation::CcmNonce>;

    /// The peer's established SPAN inner state (Key‖V), for persisting across a
    /// restart; std::nullopt if no SPAN is established for that peer.
    [[nodiscard]] auto exportSpan(std::uint8_t peer) const -> std::optional<SPAN::InnerState>;

    /// Restore a peer's SPAN from a previously exported inner state (e.g. loaded
    /// from disk at startup), so traffic resumes in lockstep without a resync.
    auto importSpan(std::uint8_t peer, const SPAN::InnerState& state) -> void;

  private:
    struct Peer
    {
        PeerConfig config;
        std::optional<SPAN::Span> span;
        std::optional<SPAN::EntropyInput> ourRei;   // REI we issued, awaiting the peer's SEI
        std::optional<SPAN::EntropyInput> peerRei;  // peer's REI, ready for us to establish on send
        std::uint8_t txSequence = 0;
    };

    auto peerState(std::uint8_t peer) -> Peer&;
    [[nodiscard]] static auto truncatedNonce(SPAN::Span& span) -> Encapsulation::CcmNonce;

    EntropySource entropy_;
    std::map<std::uint8_t, Peer> peers_;
};
}  // namespace S2

#endif  // ZWAVED_S2_SPAN_MANAGER_HPP
