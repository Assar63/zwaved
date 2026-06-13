#ifndef ZWAVED_S2_KEY_INSTALL_HPP
#define ZWAVED_S2_KEY_INSTALL_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"       // S2::Crypto::Key
#include "NetworkKeys.hpp"  // S2::NetworkKeys::Class
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Security S2 (CC 0x9F) network-key install codec — phase 7 of the S2 epic
/// (#27 / #185). Once the temporary secure channel is up, the joining node
/// requests each granted class key (NETWORK_KEY_GET), the controller transfers
/// it (NETWORK_KEY_REPORT), and the node proves installation (NETWORK_KEY_VERIFY,
/// encrypted under the new key); each exchange and the whole bootstrap are
/// closed with TRANSFER_END. Wire format per SDS13783 §4.2.6.7.6-9.
namespace S2::KeyInstall
{
constexpr std::uint8_t COMMAND_CLASS      = 0x9F;
constexpr std::uint8_t NETWORK_KEY_GET    = 0x09;
constexpr std::uint8_t NETWORK_KEY_REPORT = 0x0A;
constexpr std::uint8_t NETWORK_KEY_VERIFY = 0x0B;
constexpr std::uint8_t TRANSFER_END       = 0x0C;

// TRANSFER_END properties bits.
constexpr std::uint8_t TRANSFER_KEY_REQUEST_COMPLETE = 0x01;  // bit0
constexpr std::uint8_t TRANSFER_KEY_VERIFIED         = 0x02;  // bit1

struct KeyReport
{
    std::uint8_t grantedKey = 0;  // Table 4.19 key-class bit
    Crypto::Key key{};

    friend auto operator==(const KeyReport&, const KeyReport&) -> bool = default;
};

struct TransferEnd
{
    bool keyVerified        = false;
    bool keyRequestComplete = false;

    friend auto operator==(const TransferEnd&, const TransferEnd&) -> bool = default;
};

[[nodiscard]] auto encodeKeyGet(std::uint8_t requestedKeyBit) -> std::vector<std::uint8_t>;
[[nodiscard]] auto encodeKeyReport(std::uint8_t grantedKeyBit, const Crypto::Key& key) -> std::vector<std::uint8_t>;
[[nodiscard]] auto encodeKeyVerify() -> std::vector<std::uint8_t>;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the two TRANSFER_END flags are distinct
[[nodiscard]] auto encodeTransferEnd(bool keyVerified, bool keyRequestComplete) -> std::vector<std::uint8_t>;

[[nodiscard]] auto decodeKeyGet(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>;
[[nodiscard]] auto decodeKeyReport(std::span<const std::uint8_t> payload) -> std::optional<KeyReport>;
[[nodiscard]] auto decodeTransferEnd(std::span<const std::uint8_t> payload) -> std::optional<TransferEnd>;

/// The S2 command byte (`payload[1]`) when `payload` is a Security 2 (0x9F)
/// frame, else std::nullopt.
[[nodiscard]] auto commandByte(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>;

/// Map a single Table 4.19 key-class bit to the NetworkKeys class whose stored
/// key answers a NETWORK_KEY_GET; std::nullopt for an unknown/multi-bit value.
[[nodiscard]] auto classForKeyBit(std::uint8_t keyBit) -> std::optional<NetworkKeys::Class>;
}  // namespace S2::KeyInstall

#endif  // ZWAVED_S2_KEY_INSTALL_HPP
