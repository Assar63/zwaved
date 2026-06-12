#ifndef ZWAVED_S0_CRYPTO_HPP
#define ZWAVED_S0_CRYPTO_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/// Security S0 (CC 0x98) crypto primitives — phase 1 of the S0 epic (#26 / #162).
///
/// These are the raw building blocks the rest of the S0 pipeline composes;
/// frame assembly (authentication-data layout, nonce handling) lives in later
/// phases. Per SDS10865, S0 uses:
///   - AES-128-OFB for payload encryption (symmetric — the same call decrypts),
///   - AES-128 CBC-MAC (raw, IV=0, 8-byte truncated) for authentication,
///   - AES-128-ECB to derive the encryption key Ke and MAC key Ka from the
///     single network key (Ke = ECB(K, 0xAA·16), Ka = ECB(K, 0x55·16)).
///
/// Implemented over OpenSSL libcrypto, wrapped behind this tight interface so a
/// future swap to mbedTLS on a constrained target stays a one-file change.
namespace S0::Crypto
{
constexpr std::size_t KEY_SIZE   = 16;  // AES-128
constexpr std::size_t BLOCK_SIZE = 16;
constexpr std::size_t MAC_SIZE   = 8;  // S0 truncates the CBC-MAC to 8 bytes

using Key   = std::array<std::uint8_t, KEY_SIZE>;
using Block = std::array<std::uint8_t, BLOCK_SIZE>;
using Mac   = std::array<std::uint8_t, MAC_SIZE>;

/// The S0 encryption (Ke) and authentication (Ka) keys derived from the
/// 16-byte network key.
struct DerivedKeys
{
    Key encryption;
    Key authentication;
};

/// Encrypt one 16-byte block with AES-128 in ECB mode. The primitive behind
/// key derivation; also the per-block step a CBC-MAC reference is built from.
[[nodiscard]] auto ecbEncryptBlock(const Key& key, const Block& input) -> Block;

/// Derive the S0 Ke / Ka pair from the network key:
///   Ke = ECB(networkKey, 0xAA·16), Ka = ECB(networkKey, 0x55·16).
[[nodiscard]] auto deriveKeys(const Key& networkKey) -> DerivedKeys;

/// AES-128-OFB over `data` with the given key and 16-byte IV `initVector` (for
/// S0 the IV is senderNonce·8 ‖ receiverNonce·8). OFB is symmetric, so this
/// both encrypts and decrypts; the output is the same length as the input.
[[nodiscard]] auto ofbCrypt(const Key& key,
                            const Block& initVector,
                            std::span<const std::uint8_t> data) -> std::vector<std::uint8_t>;

/// AES-128 CBC-MAC over `data` (zero-padded to a block multiple) with IV=0,
/// truncated to the leading 8 bytes — the S0 authentication tag.
[[nodiscard]] auto cbcMac(const Key& key, std::span<const std::uint8_t> data) -> Mac;
}  // namespace S0::Crypto

#endif  // ZWAVED_S0_CRYPTO_HPP
