#include "ManufacturerSpecific.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeGet body lives in ManufacturerSpecific.gen.cpp.

namespace
{
constexpr unsigned BITS_PER_BYTE = 8;

// Byte offsets of the three u16 fields inside a Report payload.
constexpr std::size_t OFFSET_MANUFACTURER_ID = 2;
constexpr std::size_t OFFSET_PRODUCT_TYPE_ID = 4;
constexpr std::size_t OFFSET_PRODUCT_ID      = 6;

// CC + cmd + manufacturerId (2) + productTypeId (2) + productId (2).
constexpr std::size_t REPORT_BYTES = 8;

// Read a big-endian u16 from `payload` starting at `offset`. Caller
// has already bounds-checked.
auto readBeU16(std::span<const std::uint8_t> payload, std::size_t offset) -> std::uint16_t
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(payload[offset]) << BITS_PER_BYTE) |
                                      static_cast<std::uint16_t>(payload[offset + 1]));
}
}  // namespace

auto ManufacturerSpecific::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_BYTES || payload[0] != COMMAND_CLASS || payload[1] != MANUFACTURER_SPECIFIC_REPORT)
    {
        return std::nullopt;
    }
    Report out;
    out.manufacturerId = readBeU16(payload, OFFSET_MANUFACTURER_ID);
    out.productTypeId  = readBeU16(payload, OFFSET_PRODUCT_TYPE_ID);
    out.productId      = readBeU16(payload, OFFSET_PRODUCT_ID);
    return out;
}
