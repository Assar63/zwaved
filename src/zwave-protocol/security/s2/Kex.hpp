#ifndef ZWAVED_S2_KEX_HPP
#define ZWAVED_S2_KEX_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Security S2 (CC 0x9F) KEX handshake codec — phase 5 of the S2 epic (#27 / #183).
/// The inclusion key-exchange negotiation: KEX_GET/REPORT/SET/FAIL plus the
/// controller's grant policy. Wire format per SDS13783 §4.2.6.7.
namespace S2::Kex
{
constexpr std::uint8_t COMMAND_CLASS = 0x9F;
constexpr std::uint8_t KEX_GET       = 0x04;
constexpr std::uint8_t KEX_REPORT    = 0x05;
constexpr std::uint8_t KEX_SET       = 0x06;
constexpr std::uint8_t KEX_FAIL      = 0x07;

// Properties byte bits (KEX_REPORT / KEX_SET).
constexpr std::uint8_t PROP_ECHO        = 0x01;  // bit0
constexpr std::uint8_t PROP_REQUEST_CSA = 0x02;  // bit1

// Supported KEX Schemes bitmask (Table 4.17): only Scheme 1 is defined.
constexpr std::uint8_t KEX_SCHEME_1 = 0x02;  // bit1
// Supported ECDH Profiles bitmask (Table 4.18): only Curve25519 is defined.
constexpr std::uint8_t ECDH_CURVE25519 = 0x01;  // bit0

// Key-class bitmask (Table 4.19), shared by Requested/Granted Keys.
constexpr std::uint8_t KEY_S2_UNAUTHENTICATED = 0x01;  // bit0
constexpr std::uint8_t KEY_S2_AUTHENTICATED   = 0x02;  // bit1
constexpr std::uint8_t KEY_S2_ACCESS_CONTROL  = 0x04;  // bit2
constexpr std::uint8_t KEY_S0                 = 0x80;  // bit7

// KEX Fail types (Table 4.20).
constexpr std::uint8_t FAIL_KEY        = 0x01;  // no requested/granted key match
constexpr std::uint8_t FAIL_SCHEME     = 0x02;  // no supported scheme
constexpr std::uint8_t FAIL_CURVES     = 0x03;  // no supported curve
constexpr std::uint8_t FAIL_DECRYPT    = 0x05;  // decryption failure
constexpr std::uint8_t FAIL_CANCEL     = 0x06;  // user cancelled
constexpr std::uint8_t FAIL_AUTH       = 0x07;  // echo mismatch / wrong security level
constexpr std::uint8_t FAIL_KEY_GET    = 0x08;  // ungranted key requested
constexpr std::uint8_t FAIL_KEY_VERIFY = 0x09;  // including node couldn't decrypt NETWORK_KEY_VERIFY

/// KEX_REPORT contents (the joining node's advertisement).
struct Report
{
    bool echo                     = false;
    bool requestCsa               = false;
    std::uint8_t supportedSchemes = 0;
    std::uint8_t supportedCurves  = 0;
    std::uint8_t requestedKeys    = 0;

    friend auto operator==(const Report&, const Report&) -> bool = default;
};

/// KEX_SET contents (the controller's grant).
struct Set
{
    bool echo                   = false;
    bool requestCsa             = false;
    std::uint8_t selectedScheme = 0;
    std::uint8_t selectedCurve  = 0;
    std::uint8_t grantedKeys    = 0;

    friend auto operator==(const Set&, const Set&) -> bool = default;
};

[[nodiscard]] auto encodeGet() -> std::vector<std::uint8_t>;
[[nodiscard]] auto encodeReport(const Report& report) -> std::vector<std::uint8_t>;
[[nodiscard]] auto encodeSet(const Set& set) -> std::vector<std::uint8_t>;
[[nodiscard]] auto encodeFail(std::uint8_t failType) -> std::vector<std::uint8_t>;

[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
[[nodiscard]] auto decodeSet(std::span<const std::uint8_t> payload) -> std::optional<Set>;
[[nodiscard]] auto decodeFail(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>;

/// The S2 command byte (`payload[1]`) when `payload` is a Security 2 (0x9F)
/// frame, else std::nullopt.
[[nodiscard]] auto commandByte(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>;

/// Grant policy: granted = requested ∩ what the controller supports, then deny
/// the Access Control class to a node that can't do DSK confirmation (one that
/// requested Client-Side Authentication, i.e. has no DSK label).
[[nodiscard]] auto grantKeys(const Report& report, std::uint8_t controllerSupportedKeys) -> std::uint8_t;
}  // namespace S2::Kex

#endif  // ZWAVED_S2_KEX_HPP
