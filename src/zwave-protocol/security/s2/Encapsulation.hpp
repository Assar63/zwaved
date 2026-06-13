#ifndef ZWAVED_S2_ENCAPSULATION_HPP
#define ZWAVED_S2_ENCAPSULATION_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"  // S2::Crypto::Key
// IWYU pragma: end_exports

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Security S2 (CC 0x9F) MESSAGE_ENCAPSULATION (0x03) codec — phase 4 (#27 / #182).
///
/// Frame (SDS13783 §4.2.6.5.11):
///   [0x9F][0x03][seq][props][non-encrypted extensions…][CCM ciphertext ‖ tag]
/// where props bit0 = non-encrypted extensions present, bit1 = encrypted
/// extensions present (these live inside the ciphertext). The CCM nonce is the
/// SPAN's NextNonce truncated to 13 bytes; the key is the granted class's
/// KeyCCM. The AAD (§4.2.6.4.6) binds sender/receiver/home ids, the total frame
/// length, the sequence number, the props byte and the non-encrypted extension
/// bytes — so a tampered header or a wrong identity fails authentication.
///
/// Pure functions composing S2::Crypto; the SPAN advance + SPAN-extension
/// interpretation (nonce sync) live in the ProtocolThread integration (phase 8).
namespace S2::Encapsulation
{
constexpr std::size_t CCM_NONCE_SIZE = 13;
constexpr std::size_t TAG_SIZE       = 8;

using CcmNonce = std::array<std::uint8_t, CCM_NONCE_SIZE>;

/// The frame's identity fields, which the AAD authenticates.
struct Context
{
    std::uint8_t senderNodeId   = 0;
    std::uint8_t receiverNodeId = 0;  // Destination Tag (Receiver NodeID for singlecast)
    std::array<std::uint8_t, 4> homeId{};
    std::uint8_t sequenceNumber = 0;
};

/// Build a MESSAGE_ENCAPSULATION frame carrying `inner`, optionally preceded by
/// already-formatted `nonEncryptedExtensions` (e.g. a SPAN extension). The CCM
/// nonce comes from the peer's SPAN; the key is the class KeyCCM.
[[nodiscard]] auto encrypt(std::span<const std::uint8_t> inner,
                           const Context& context,
                           const Crypto::Key& classKey,
                           const CcmNonce& nonce,
                           std::span<const std::uint8_t> nonEncryptedExtensions = {}) -> std::vector<std::uint8_t>;

/// Authenticate + decrypt a MESSAGE_ENCAPSULATION frame, returning the inner CC
/// command. std::nullopt on any malformation or authentication failure (wrong
/// key / nonce / identity, tampered bytes) — the frame drops silently. Any
/// encrypted extensions are stripped from the front of the plaintext.
[[nodiscard]] auto decrypt(std::span<const std::uint8_t> frame,
                           const Context& context,
                           const Crypto::Key& classKey,
                           const CcmNonce& nonce) -> std::optional<std::vector<std::uint8_t>>;
}  // namespace S2::Encapsulation

#endif  // ZWAVED_S2_ENCAPSULATION_HPP
