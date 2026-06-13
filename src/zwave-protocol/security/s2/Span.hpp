#ifndef ZWAVED_S2_SPAN_HPP
#define ZWAVED_S2_SPAN_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"  // S2::Crypto::Key / Block
// IWYU pragma: end_exports

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

/// Security S2 (CC 0x9F) SPAN — Singlecast Pre-Agreed Nonce (phase 3, #27 / #181).
///
/// Two nodes exchange 16-byte Entropy Inputs (EIs), mix them into a 32-byte MEI
/// (CKDF-MEI-Extract/Expand, SDS13783 §4.2.6.4.18-19), and instantiate a shared
/// AES-128 CTR_DRBG (no derivation function) seeded by the MEI and the
/// per-network PersonalizationString. Each side then draws nonces from the DRBG
/// in lockstep — "pre-agreed" — so no per-frame nonce exchange is needed. A
/// desync (missed frame / restart without persisted state) is recovered by a
/// Nonce Sync that re-exchanges EIs and re-instantiates the SPAN.
///
/// This is the spec-exact derivation; interop is verified on hardware (#189).
namespace S2::SPAN
{
constexpr std::size_t EI_SIZE              = 16;
constexpr std::size_t MEI_SIZE             = 32;
constexpr std::size_t PERSONALIZATION_SIZE = 32;
constexpr std::size_t NONCE_SIZE           = 16;  // DRBG output; truncated to 13 for CCM by the caller

using EntropyInput    = std::array<std::uint8_t, EI_SIZE>;
using Mei             = std::array<std::uint8_t, MEI_SIZE>;
using Personalization = std::array<std::uint8_t, PERSONALIZATION_SIZE>;
using Nonce           = std::array<std::uint8_t, NONCE_SIZE>;
using InnerState      = std::array<std::uint8_t, 2 * Crypto::BLOCK_SIZE>;  // CTR_DRBG Key‖V

/// CKDF-MEI: mix the two entropy inputs into the 32-byte MEI.
[[nodiscard]] auto mixEntropy(const EntropyInput& senderEI, const EntropyInput& receiverEI) -> Mei;

/// Increment a 16-byte big-endian counter in place (wraps all-ones to all-zeros).
auto incrementCounter(Crypto::Block& counter) -> void;

/// One peer's SPAN: the inner AES-128 CTR_DRBG working state.
class Span
{
  public:
    /// Instantiate the CTR_DRBG from the two EIs (mixed into the MEI) and the
    /// per-network personalization string.
    [[nodiscard]] static auto instantiate(const EntropyInput& senderEI,
                                          const EntropyInput& receiverEI,
                                          const Personalization& personalization) -> Span;

    /// The next 16-byte nonce, advancing the inner state (NextNonce). Callers
    /// truncate to the 13 most-significant bytes before the CCM module.
    [[nodiscard]] auto nextNonce() -> Nonce;

    /// The 32-byte inner state (Key‖V), for persisting across a restart.
    [[nodiscard]] auto serialize() const -> InnerState;
    [[nodiscard]] static auto deserialize(const InnerState& state) -> Span;

  private:
    Crypto::Key key_{};
    Crypto::Block value_{};
};

/// Per-peer SPAN store (in memory). Disk persistence + the Nonce Sync bus flow
/// are wired in with the ProtocolThread integration (phase 8); `serialize` /
/// `deserialize` are the persistence primitives those will use.
class Table
{
  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the two EIs are distinct directional roles
    auto establish(std::uint8_t peer,
                   const EntropyInput& senderEI,
                   const EntropyInput& receiverEI,
                   const Personalization& personalization) -> void;
    [[nodiscard]] auto has(std::uint8_t peer) const -> bool;
    /// Next nonce for `peer`, advancing its SPAN; std::nullopt if none established.
    [[nodiscard]] auto nextNonce(std::uint8_t peer) -> std::optional<Nonce>;
    /// Drop a peer's SPAN (e.g. on a decapsulation failure, forcing a resync).
    auto remove(std::uint8_t peer) -> void;

  private:
    std::map<std::uint8_t, Span> spans_;
};

/// Process-wide SPAN table.
[[nodiscard]] auto table() -> Table&;
}  // namespace S2::SPAN

#endif  // ZWAVED_S2_SPAN_HPP
