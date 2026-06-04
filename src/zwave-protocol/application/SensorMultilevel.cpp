#include "SensorMultilevel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeGet body lives in SensorMultilevel.gen.cpp (empty v1 payload).

namespace
{
constexpr unsigned BITS_PER_BYTE = 8;

// Report header before the variable-length value: CC + cmd + sensorType
// + flag byte = 4 bytes; the signed value follows.
constexpr std::size_t REPORT_HEADER_BYTES = 4;
constexpr std::size_t OFFSET_SENSOR_TYPE  = 2;
constexpr std::size_t OFFSET_FLAGS        = 3;
constexpr std::size_t OFFSET_VALUE        = 4;

constexpr std::uint8_t SIZE_1_BYTE = 1;
constexpr std::uint8_t SIZE_2_BYTE = 2;
constexpr std::uint8_t SIZE_4_BYTE = 4;

auto isValidSize(std::uint8_t size) -> bool
{
    return size == SIZE_1_BYTE || size == SIZE_2_BYTE || size == SIZE_4_BYTE;
}

// MSB of a `size`-byte field, for sign detection.
auto signBit(std::uint8_t size) -> std::uint32_t
{
    return std::uint32_t{1} << ((static_cast<unsigned>(size) * BITS_PER_BYTE) - 1U);
}

// Low-`size`-bytes mask: 0xFF, 0xFFFF, 0xFFFFFFFF. (`1U << 32` is UB, so
// size=4 returns ~0 directly.)
auto valueMask(std::uint8_t size) -> std::uint32_t
{
    if (size == SIZE_4_BYTE)
    {
        return ~std::uint32_t{0};
    }
    return (std::uint32_t{1} << (static_cast<unsigned>(size) * BITS_PER_BYTE)) - 1U;
}
}  // namespace

auto SensorMultilevel::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS || payload[1] != SENSOR_MULTILEVEL_REPORT)
    {
        return std::nullopt;
    }
    const std::uint8_t flags = payload[OFFSET_FLAGS];
    const auto size          = static_cast<std::uint8_t>(flags & FLAG_SIZE_MASK);
    if (!isValidSize(size) || payload.size() < static_cast<std::size_t>(REPORT_HEADER_BYTES) + size)
    {
        return std::nullopt;
    }

    // Read `size` big-endian bytes, then sign-extend from the field's MSB
    // (same scheme as Configuration's variable-width signed value).
    std::uint32_t raw = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        raw = (raw << BITS_PER_BYTE) | std::uint32_t{payload[OFFSET_VALUE + i]};
    }
    if ((raw & signBit(size)) != 0U)
    {
        raw |= ~valueMask(size);
    }

    Report out;
    out.sensorType = payload[OFFSET_SENSOR_TYPE];
    out.scale      = static_cast<std::uint8_t>((flags & FLAG_SCALE_MASK) >> FLAG_SCALE_SHIFT);
    out.precision  = static_cast<std::uint8_t>((flags & FLAG_PRECISION_MASK) >> FLAG_PRECISION_SHIFT);
    out.value      = static_cast<std::int32_t>(raw);
    return out;
}
