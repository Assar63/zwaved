#ifndef ZWAVED_S0_ENCAPSULATION_HPP
#define ZWAVED_S0_ENCAPSULATION_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"      // Crypto::Key
#include "NonceTable.hpp"  // S0::Nonce
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Security S0 (CC 0x98) MESSAGE_ENCAPSULATION (0x81) codec — phase 4 of the S0
/// epic (#26 / #165). Composes the phase-1 primitives into the encrypted
/// wrapper that actually carries an inner CC frame over the air:
///
///   [0x98][0x81][senderNonce·8][ciphertext…][receiverNonceId·1][MAC·8]
///
/// where ciphertext = AES-128-OFB(Ke, IV = senderNonce‖receiverNonce) over
/// [sequenceByte][inner], and the 8-byte MAC = AES-128 CBC-MAC(Ka) over
/// [senderNonce‖receiverNonce, 0x81, senderNodeId, receiverNodeId,
/// len(ciphertext), ciphertext] (SDS10865). Pure functions — the inbound
/// dispatch + outbound nonce/key plumbing is phase 5.
namespace S0::Encapsulation
{
/// Build a MESSAGE_ENCAPSULATION frame carrying `inner`. `senderNonce` is our
/// fresh random nonce (transmitted as the frame IV); `receiverNonce` is the
/// peer's nonce from its NONCE_REPORT (only its first byte travels, as the
/// receiver-nonce identifier). Ke/Ka are derived from `networkKey`.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): node ids / nonces are distinct roles documented above
[[nodiscard]] auto encrypt(std::span<const std::uint8_t> inner,
                           std::uint8_t senderNodeId,
                           std::uint8_t receiverNodeId,
                           const Nonce& senderNonce,
                           const Nonce& receiverNonce,
                           const Crypto::Key& networkKey) -> std::vector<std::uint8_t>;

/// Authenticate and decrypt a received MESSAGE_ENCAPSULATION frame. `ourNonce`
/// is the nonce we previously issued to this peer (looked up by the frame's
/// receiver-nonce id). Returns the inner CC frame, or std::nullopt on any
/// malformation, a receiver-nonce-id mismatch, or a MAC mismatch — a tampered
/// or stale frame drops silently.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): senderNodeId / receiverNodeId are distinct roles
[[nodiscard]] auto decrypt(std::span<const std::uint8_t> frame,
                           std::uint8_t senderNodeId,
                           std::uint8_t receiverNodeId,
                           const Nonce& ourNonce,
                           const Crypto::Key& networkKey) -> std::optional<std::vector<std::uint8_t>>;
}  // namespace S0::Encapsulation

#endif  // ZWAVED_S0_ENCAPSULATION_HPP
