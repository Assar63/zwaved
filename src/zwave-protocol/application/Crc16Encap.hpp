#ifndef ZWAVED_CRC16_ENCAP_HPP
#define ZWAVED_CRC16_ENCAP_HPP

// IWYU pragma: begin_exports
#include "Crc16Encap.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave CRC-16 Encapsulation Command Class (0x56) — a pre-S0 integrity
/// wrapper (#28). CRC16_ENCAP (0x01) carries an inner CC frame plus a
/// trailing 2-byte big-endian CRC-16/AUG-CCITT over the whole command
/// (CC + cmd + inner). Transport-only: in practice the daemon only
/// verifies inbound frames and unwraps the inner CC. Constants come from
/// Crc16Encap.gen.hpp.
namespace Crc16Encap
{
/// CRC-16/AUG-CCITT (poly 0x1021, init 0x1D0F, no reflection, no XOR-out)
/// over `data`. This is the algorithm CC 0x56 uses — distinct from the
/// CRC-16-CCITT (init 0xFFFF) used by Transport Service (CC 0x55).
[[nodiscard]] auto checksum(std::span<const std::uint8_t> data) -> std::uint16_t;

/// Verify a CRC16_ENCAP payload (bytes inside an APPLICATION_COMMAND_HANDLER
/// frame, starting with COMMAND_CLASS) and return the unwrapped inner CC
/// frame. Returns std::nullopt if the bytes are not a CRC16_ENCAP, are too
/// short, or fail the CRC check.
[[nodiscard]] auto verifyAndUnwrap(std::span<const std::uint8_t> payload) -> std::optional<std::vector<std::uint8_t>>;

/// Wrap `innerCommand` (a complete CC frame) in a CRC16_ENCAP, appending
/// the computed CRC. Mirrors verifyAndUnwrap.
[[nodiscard]] auto encode(std::span<const std::uint8_t> innerCommand) -> std::vector<std::uint8_t>;
}  // namespace Crc16Encap

#endif  // ZWAVED_CRC16_ENCAP_HPP
