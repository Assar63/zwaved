#ifndef ZWAVED_S2_NONCE_SYNC_HPP
#define ZWAVED_S2_NONCE_SYNC_HPP

// IWYU pragma: begin_exports
#include "Span.hpp"  // S2::SPAN::EntropyInput
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Security S2 (CC 0x9F) nonce synchronisation — the SPAN-establishment wire
/// codec (part of #187 / #199, SDS13783 §4.2.6.5.9-10 + §4.2.6.5.13).
///
/// Before two nodes can exchange MESSAGE_ENCAPSULATION frames they must share a
/// SPAN (Singlecast Pre-Agreed Nonce, see Span.hpp). It is bootstrapped by
/// trading 16-byte Entropy Inputs:
///   - NONCE_GET asks the peer for a fresh nonce;
///   - NONCE_REPORT carries the Receiver's Entropy Input (REI) when the SOS
///     (SPAN out of sync) flag is set;
///   - the SPAN Extension, ridden along on the next MESSAGE_ENCAPSULATION,
///     carries the Sender's Entropy Input (SEI).
/// Both EIs mix (Span::instantiate) into the shared SPAN generator.
///
/// Pure codec; the runtime glue (SPAN table updates, when to set SOS, attaching
/// the extension on send) lives in the ProtocolThread / bootstrap integration.
namespace S2::NonceSync
{
constexpr std::uint8_t COMMAND_CLASS = 0x9F;
constexpr std::uint8_t NONCE_GET     = 0x01;
constexpr std::uint8_t NONCE_REPORT  = 0x02;

// NONCE_REPORT flag bits.
constexpr std::uint8_t FLAG_SOS = 0x01;  // bit0 — SPAN out of sync; the REI field is present
constexpr std::uint8_t FLAG_MOS = 0x02;  // bit1 — MPAN out of sync (multicast; carried, not acted on yet)

// SPAN Extension object (an unencrypted extension on MESSAGE_ENCAPSULATION).
constexpr std::uint8_t SPAN_EXTENSION_TYPE   = 0x01;
constexpr std::uint8_t SPAN_EXTENSION_LENGTH = 18;    // length byte value: 1 len + 1 flags/type + 16 SEI
constexpr std::uint8_t SPAN_EXTENSION_FLAGS  = 0x41;  // moreToFollow=0, critical=1, type=SPAN(1)

// Extension header byte layout (shared by all extension objects).
constexpr std::uint8_t EXT_TYPE_MASK       = 0x3F;  // bits0-5
constexpr std::uint8_t EXT_CRITICAL_BIT    = 0x40;  // bit6
constexpr std::uint8_t EXT_MORE_FOLLOW_BIT = 0x80;  // bit7

using EntropyInput = SPAN::EntropyInput;

/// NONCE_REPORT contents.
struct Report
{
    std::uint8_t sequenceNumber = 0;
    bool sos                    = false;
    bool mos                    = false;
    std::optional<EntropyInput> rei;  // present iff sos (the Receiver's Entropy Input)

    friend auto operator==(const Report&, const Report&) -> bool = default;
};

/// `[0x9F][0x01][seq]`.
[[nodiscard]] auto encodeNonceGet(std::uint8_t sequenceNumber) -> std::vector<std::uint8_t>;

/// `[0x9F][0x02][seq][flags]` + the 16-byte REI when `sos` is set.
[[nodiscard]] auto encodeNonceReport(const Report& report) -> std::vector<std::uint8_t>;

/// Parse a NONCE_REPORT; std::nullopt if malformed (wrong CC/command, too short,
/// or SOS set without a full REI).
[[nodiscard]] auto decodeNonceReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;

/// A single SPAN Extension object: `[18][0x41][SEI·16]`.
[[nodiscard]] auto encodeSpanExtension(const EntropyInput& senderEntropyInput) -> std::vector<std::uint8_t>;

/// Scan a buffer of concatenated unencrypted extension objects for a SPAN
/// Extension and return its SEI; std::nullopt if none (or the buffer is
/// malformed).
[[nodiscard]] auto findSpanExtension(std::span<const std::uint8_t> extensions) -> std::optional<EntropyInput>;

/// The S2 command byte (`payload[1]`) when `payload` is a Security 2 (0x9F)
/// frame, else std::nullopt.
[[nodiscard]] auto commandByte(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>;
}  // namespace S2::NonceSync

#endif  // ZWAVED_S2_NONCE_SYNC_HPP
