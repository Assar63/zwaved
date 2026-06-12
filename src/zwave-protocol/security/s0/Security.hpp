#ifndef ZWAVED_S0_SECURITY_HPP
#define ZWAVED_S0_SECURITY_HPP

// IWYU pragma: begin_exports
#include "NonceTable.hpp"    // S0::Nonce
#include "Security.gen.hpp"  // SECURITY_* command bytes + COMMAND_CLASS
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Security S0 (CC 0x98) wire codec — phase 3 (#164) covers the nonce
/// handshake commands; the encrypted MESSAGE_ENCAPSULATION codec is phase 4.
/// Command-byte constants come from Security.gen.hpp. The namespace matches the
/// manifest module name (the project convention for CC codecs), so the
/// generated constants resolve unqualified here; the nonce type itself lives in
/// S0:: alongside the table that mints it.
namespace Security
{
/// SECURITY_NONCE_GET — a node asks us for a nonce before sending us an
/// encrypted frame. No payload beyond [COMMAND_CLASS, SECURITY_NONCE_GET].
[[nodiscard]] auto encodeNonceGet() -> std::vector<std::uint8_t>;

/// SECURITY_NONCE_REPORT — our reply carrying the 8-byte nonce we issued:
/// [COMMAND_CLASS, SECURITY_NONCE_REPORT, nonce[0..7]].
[[nodiscard]] auto encodeNonceReport(const S0::Nonce& nonce) -> std::vector<std::uint8_t>;

/// Parse a SECURITY_NONCE_REPORT frame, returning the carried nonce, or
/// std::nullopt if it isn't a well-formed 0x98/0x80 frame of the right length.
[[nodiscard]] auto decodeNonceReport(std::span<const std::uint8_t> payload) -> std::optional<S0::Nonce>;

/// The S0 command byte (`payload[1]`) when `payload` is a Security (0x98)
/// frame, else std::nullopt — lets a dispatcher branch without re-checking the
/// class byte everywhere.
[[nodiscard]] auto commandByte(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>;
}  // namespace Security

#endif  // ZWAVED_S0_SECURITY_HPP
