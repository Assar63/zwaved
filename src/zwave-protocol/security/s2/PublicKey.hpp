#ifndef ZWAVED_S2_PUBLIC_KEY_HPP
#define ZWAVED_S2_PUBLIC_KEY_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"  // S2::Crypto::PublicKey
// IWYU pragma: end_exports

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// Security S2 (CC 0x9F) public-key exchange + DSK ritual — phase 6 (#27 / #184).
///
/// Both nodes swap their Curve25519 public keys via PUBLIC_KEY_REPORT (0x08) to
/// derive the ECDH shared secret. For the Authenticated / Access Control
/// classes the joining node obfuscates the first 2 bytes of its key (the DSK
/// PIN); the operator reads the DSK off the device label and types the PIN, and
/// the controller restores those bytes before the ECDH — proving the operator
/// is looking at the real device. A wrong PIN yields the wrong shared secret,
/// so the temporary secure channel simply fails to come up (→ KEX_FAIL later).
///
/// Pure codec + DSK helpers. The pending-DSK state, the GetPendingDSK /
/// ConfirmDSK D-Bus surface, the terminal prompt, and the KEX_FAIL-on-bad-PIN
/// wiring live in the inclusion bootstrap (phase 9), which holds the per-node
/// state these helpers operate on.
namespace S2::PublicKey
{
constexpr std::uint8_t COMMAND_CLASS       = 0x9F;
constexpr std::uint8_t PUBLIC_KEY_REPORT   = 0x08;
constexpr std::uint8_t PROP_INCLUDING_NODE = 0x01;  // bit0

constexpr std::size_t OBFUSCATE_NONE = 0;  // Unauthenticated / S0
constexpr std::size_t OBFUSCATE_DSK  = 2;  // Authenticated / Access Control (DSK PIN)
constexpr std::size_t OBFUSCATE_CSA  = 4;  // Client-Side Authentication

struct Report
{
    bool includingNode = false;
    Crypto::PublicKey key{};

    friend auto operator==(const Report&, const Report&) -> bool = default;
};

/// Build a PUBLIC_KEY_REPORT. `obfuscateLeading` zeros that many leading key
/// bytes on the wire (OBFUSCATE_DSK / OBFUSCATE_CSA / OBFUSCATE_NONE).
[[nodiscard]] auto encode(bool includingNode,
                          const Crypto::PublicKey& key,
                          std::size_t obfuscateLeading) -> std::vector<std::uint8_t>;

/// Parse a PUBLIC_KEY_REPORT; std::nullopt if malformed.
[[nodiscard]] auto decode(std::span<const std::uint8_t> payload) -> std::optional<Report>;

/// The full DSK string of a public key: 8 hyphen-separated groups of 5 decimal
/// digits, each group the big-endian value of 2 bytes (the first 16 bytes).
[[nodiscard]] auto dskString(const Crypto::PublicKey& key) -> std::string;

/// The 5-digit DSK PIN — the first group (first 2 bytes) of the DSK string.
[[nodiscard]] auto dskPin(const Crypto::PublicKey& key) -> std::string;

/// Parse an operator-entered 5-digit PIN into its 16-bit value; std::nullopt if
/// it isn't exactly 5 digits or exceeds 65535.
[[nodiscard]] auto parsePin(const std::string& pin) -> std::optional<std::uint16_t>;

/// Restore the first 2 bytes of an obfuscated public key from the PIN value, so
/// the result is the joining node's true key for ECDH.
[[nodiscard]] auto applyPin(Crypto::PublicKey obfuscatedKey, std::uint16_t pin) -> Crypto::PublicKey;
}  // namespace S2::PublicKey

#endif  // ZWAVED_S2_PUBLIC_KEY_HPP
