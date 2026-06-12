#ifndef ZWAVED_S2_CRYPTO_HPP
#define ZWAVED_S2_CRYPTO_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Security S2 (CC 0x9F) crypto primitives — phase 1 of the S2 epic (#27 / #179).
///
/// All over OpenSSL libcrypto (no libsodium: it has no AES-128, AES-CCM or
/// CMAC, so it can't be a standalone S2 backend, and libcrypto does X25519
/// fine). Per SDS13783, S2 uses:
///   - AES-128-CCM     — authenticated encryption of the message wrapper,
///   - AES-128-CMAC    — CKDF key derivation + SPAN/MPAN next-nonce,
///   - Curve25519 ECDH — the inclusion key exchange.
///
/// Wrapped behind this tight interface so a future mbedTLS port stays a
/// one-file swap. CCM nonce / tag lengths are caller-chosen (S2 uses a 13-byte
/// nonce and an 8-byte tag) so the primitive is reusable and testable against
/// the NIST vectors, which use different sizes.
namespace S2::Crypto
{
constexpr std::size_t KEY_SIZE            = 16;  // AES-128
constexpr std::size_t CMAC_SIZE           = 16;
constexpr std::size_t CURVE25519_KEY_SIZE = 32;

using Key          = std::array<std::uint8_t, KEY_SIZE>;
using Mac          = std::array<std::uint8_t, CMAC_SIZE>;
using PublicKey    = std::array<std::uint8_t, CURVE25519_KEY_SIZE>;
using PrivateKey   = std::array<std::uint8_t, CURVE25519_KEY_SIZE>;
using SharedSecret = std::array<std::uint8_t, CURVE25519_KEY_SIZE>;

struct KeyPair
{
    PublicKey publicKey{};
    PrivateKey privateKey{};
};

/// AES-128-CCM. Returns ciphertext ‖ tag (the tag is `tagLength` bytes).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): nonce/aad/plaintext are distinct CCM inputs
[[nodiscard]] auto ccmEncrypt(const Key& key,
                              std::span<const std::uint8_t> nonce,
                              std::span<const std::uint8_t> aad,
                              std::span<const std::uint8_t> plaintext,
                              std::size_t tagLength) -> std::vector<std::uint8_t>;

/// AES-128-CCM decrypt + verify. Input is ciphertext ‖ tag. Returns the
/// plaintext, or std::nullopt if authentication fails / the input is too short.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): nonce/aad/ciphertext are distinct CCM inputs
[[nodiscard]] auto ccmDecrypt(const Key& key,
                              std::span<const std::uint8_t> nonce,
                              std::span<const std::uint8_t> aad,
                              std::span<const std::uint8_t> ciphertextWithTag,
                              std::size_t tagLength) -> std::optional<std::vector<std::uint8_t>>;

/// AES-128-CMAC over `data`.
[[nodiscard]] auto cmac(const Key& key, std::span<const std::uint8_t> data) -> Mac;

/// Generate a Curve25519 key pair.
[[nodiscard]] auto generateKeyPair() -> KeyPair;

/// X25519 ECDH: the shared secret from our private key and the peer's public
/// key. std::nullopt only on an internal/library failure.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): private vs public key are distinct roles
[[nodiscard]] auto ecdh(const PrivateKey& ours, const PublicKey& theirs) -> std::optional<SharedSecret>;
}  // namespace S2::Crypto

#endif  // ZWAVED_S2_CRYPTO_HPP
