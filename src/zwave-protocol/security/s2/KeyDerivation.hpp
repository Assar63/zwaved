#ifndef ZWAVED_S2_KEY_DERIVATION_HPP
#define ZWAVED_S2_KEY_DERIVATION_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"  // S2::Crypto::Key / PublicKey / SharedSecret
// IWYU pragma: end_exports

#include <array>
#include <cstddef>
#include <cstdint>

/// Security S2 (CC 0x9F) CKDF key derivations — part of the inclusion bootstrap
/// (#27 / #187). Two CMAC-based KDFs from SDS13783 §4.2.6.4.10-13:
///   - the *temporary* keys (TempKeyCCM + TempPersonalizationString) protect
///     the bootstrap channel, derived from the ECDH shared secret + both public
///     keys;
///   - the *permanent* per-network-key trio (KeyCCM, PersonalizationString,
///     KeyMPAN) protects normal traffic once a class key is installed.
namespace S2::KeyDerivation
{
constexpr std::size_t PERSONALIZATION_SIZE = 32;
using Personalization                      = std::array<std::uint8_t, PERSONALIZATION_SIZE>;

/// Temporary keys for the bootstrap secure channel.
struct TempKeys
{
    Crypto::Key keyCcm{};
    Personalization personalization{};
};

/// The permanent trio derived from one network (class) key.
struct NetworkKeys
{
    Crypto::Key keyCcm{};
    Personalization personalization{};
    Crypto::Key keyMpan{};
};

/// CKDF-TempExtract: PRK = CMAC(0x33·16, ECDH_shared ‖ KeyPub_controller ‖
/// KeyPub_node). The two public keys must be in including-then-joining order.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): shared/controller/node are distinct, fixed-order inputs
[[nodiscard]] auto tempExtract(const Crypto::SharedSecret& sharedSecret,
                               const Crypto::PublicKey& controllerPublicKey,
                               const Crypto::PublicKey& nodePublicKey) -> Crypto::Key;

/// CKDF-TempExpand: derive {TempKeyCCM, TempPersonalizationString} from the PRK.
[[nodiscard]] auto tempExpand(const Crypto::Key& prk) -> TempKeys;

/// Convenience: tempExtract followed by tempExpand.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): shared/controller/node are distinct, fixed-order inputs
[[nodiscard]] auto deriveTempKeys(const Crypto::SharedSecret& sharedSecret,
                                  const Crypto::PublicKey& controllerPublicKey,
                                  const Crypto::PublicKey& nodePublicKey) -> TempKeys;

/// CKDF-NetworkKeyExpand: derive {KeyCCM, PersonalizationString, KeyMPAN} from a
/// network (class) key.
[[nodiscard]] auto networkKeyExpand(const Crypto::Key& networkKey) -> NetworkKeys;
}  // namespace S2::KeyDerivation

#endif  // ZWAVED_S2_KEY_DERIVATION_HPP
