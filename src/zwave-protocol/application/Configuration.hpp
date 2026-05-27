#ifndef ZWAVED_CONFIGURATION_HPP
#define ZWAVED_CONFIGURATION_HPP

// IWYU pragma: begin_exports
#include "Configuration.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Configuration Command Class (0x70). Vendor-defined
/// parameter store. v1 carries one `(parameter, size, value)`
/// triple per frame; `size` selects a 1, 2, or 4-byte big-endian
/// value. The spec treats values as two's-complement signed by
/// convention — the codec sign-extends on decode and truncates
/// the low `size` bytes on encode.
///
/// Set/Get/Report all carry variable-width payloads the manifest's
/// simple-encoder generator can't express, so all three are
/// hand-written here. v4 Bulk variants and the default-value flag
/// are not implemented today.
namespace Configuration
{
/// Wire-permitted sizes for the value field.
inline constexpr std::uint8_t SIZE_1_BYTE = 1;
inline constexpr std::uint8_t SIZE_2_BYTE = 2;
inline constexpr std::uint8_t SIZE_4_BYTE = 4;

struct Report
{
    std::uint8_t parameter = 0;
    std::uint8_t size      = 0;
    /// Sign-extended from `size`'s MSB. Callers who know their
    /// parameter is unsigned can `static_cast<std::uint32_t>(value)`.
    std::int32_t value = 0;
};

/// Encode CONFIGURATION_SET. `size` must be 1, 2, or 4; the
/// encoder truncates `value` to the low `size` bytes big-endian.
/// A caller passing an out-of-range size gets an empty vector
/// back — guard against this in callers (typically the bus
/// handler in ProtocolThread).
[[nodiscard]] auto encodeSet(std::uint8_t parameter,
                             std::uint8_t size,
                             std::int32_t value) -> std::vector<std::uint8_t>;

/// Encode CONFIGURATION_GET. Three bytes on the wire: CC + cmd +
/// parameter.
[[nodiscard]] auto encodeGet(std::uint8_t parameter) -> std::vector<std::uint8_t>;

/// Decode a Configuration Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt for non-matching frames, truncated
/// payloads, or invalid `size` bytes (outside {1, 2, 4}).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace Configuration

#endif  // ZWAVED_CONFIGURATION_HPP
