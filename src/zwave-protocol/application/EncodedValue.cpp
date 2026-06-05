#include "EncodedValue.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
constexpr unsigned BITS_PER_BYTE = 8;

constexpr std::uint8_t FLAG_SIZE_MASK       = 0x07;
constexpr std::uint8_t FLAG_SCALE_MASK      = 0x18;
constexpr std::uint8_t FLAG_SCALE_SHIFT     = 3;
constexpr std::uint8_t FLAG_PRECISION_MASK  = 0xE0;
constexpr std::uint8_t FLAG_PRECISION_SHIFT = 5;

constexpr std::uint8_t SCALE_FIELD_MASK     = 0x03;  // scale occupies 2 bits
constexpr std::uint8_t PRECISION_FIELD_MASK = 0x07;  // precision occupies 3 bits

constexpr std::uint8_t SIZE_1_BYTE = 1;
constexpr std::uint8_t SIZE_2_BYTE = 2;
constexpr std::uint8_t SIZE_4_BYTE = 4;

constexpr std::uint32_t LOW_BYTE_MASK = 0xFFU;

// Signed ranges that fit in a 1- / 2-byte field, for size selection.
constexpr std::int32_t INT8_MIN_VALUE  = -128;
constexpr std::int32_t INT8_MAX_VALUE  = 127;
constexpr std::int32_t INT16_MIN_VALUE = -32768;
constexpr std::int32_t INT16_MAX_VALUE = 32767;

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

auto EncodedValue::decode(std::uint8_t flags, std::span<const std::uint8_t> valueBytes) -> std::optional<Decoded>
{
    const auto size = static_cast<std::uint8_t>(flags & FLAG_SIZE_MASK);
    if (!isValidSize(size) || valueBytes.size() < size)
    {
        return std::nullopt;
    }

    std::uint32_t raw = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        raw = (raw << BITS_PER_BYTE) | std::uint32_t{valueBytes[i]};
    }
    if ((raw & signBit(size)) != 0U)
    {
        raw |= ~valueMask(size);
    }

    Decoded out;
    out.size      = size;
    out.scale     = static_cast<std::uint8_t>((flags & FLAG_SCALE_MASK) >> FLAG_SCALE_SHIFT);
    out.precision = static_cast<std::uint8_t>((flags & FLAG_PRECISION_MASK) >> FLAG_PRECISION_SHIFT);
    out.value     = static_cast<std::int32_t>(raw);
    return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): precision/scale named at the call site
auto EncodedValue::encode(std::uint8_t precision, std::uint8_t scale, std::int32_t value) -> std::vector<std::uint8_t>
{
    // Smallest size that holds the signed value.
    std::uint8_t size = SIZE_4_BYTE;
    if (value >= INT8_MIN_VALUE && value <= INT8_MAX_VALUE)
    {
        size = SIZE_1_BYTE;
    }
    else if (value >= INT16_MIN_VALUE && value <= INT16_MAX_VALUE)
    {
        size = SIZE_2_BYTE;
    }

    const auto flags = static_cast<std::uint8_t>(((precision & PRECISION_FIELD_MASK) << FLAG_PRECISION_SHIFT) |
                                                 ((scale & SCALE_FIELD_MASK) << FLAG_SCALE_SHIFT) | size);

    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(size) + 1);
    out.push_back(flags);
    const auto raw = static_cast<std::uint32_t>(value);
    for (std::size_t i = 0; i < size; ++i)
    {
        const auto shift = static_cast<unsigned>((size - 1 - i) * BITS_PER_BYTE);
        out.push_back(static_cast<std::uint8_t>((raw >> shift) & LOW_BYTE_MASK));
    }
    return out;
}
