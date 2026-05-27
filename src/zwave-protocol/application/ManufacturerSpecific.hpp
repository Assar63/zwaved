#ifndef ZWAVED_MANUFACTURER_SPECIFIC_HPP
#define ZWAVED_MANUFACTURER_SPECIFIC_HPP

// IWYU pragma: begin_exports
#include "ManufacturerSpecific.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Manufacturer Specific Command Class (0x72). Get returns
/// a Report carrying the three u16 identifiers — manufacturer ID,
/// product type ID, product ID — that together pin down which
/// physical device a node is. All three are big-endian on the wire.
///
/// v2 adds a Device Specific Get / Report pair for serial numbers
/// and OEM-specific data; not implemented today.
///
/// Constants and `encodeGet()` come from ManufacturerSpecific.gen.hpp.
namespace ManufacturerSpecific
{
struct Report
{
    std::uint16_t manufacturerId = 0;
    std::uint16_t productTypeId  = 0;
    std::uint16_t productId      = 0;
};

/// Decode a Manufacturer Specific Report payload (the bytes inside
/// an APPLICATION_COMMAND_HANDLER frame, starting with
/// COMMAND_CLASS). Returns std::nullopt if the bytes are not a
/// Manufacturer Specific Report.
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace ManufacturerSpecific

#endif  // ZWAVED_MANUFACTURER_SPECIFIC_HPP
