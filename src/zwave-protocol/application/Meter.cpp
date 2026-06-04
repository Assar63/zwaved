#include "Meter.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
constexpr unsigned BITS_PER_BYTE = 8;

// Report header before the variable-width value: CC + cmd + properties1
// + properties2(flags) = 4 bytes; the signed value follows.
constexpr std::size_t REPORT_HEADER_BYTES = 4;
constexpr std::size_t OFFSET_PROPERTIES1  = 2;
constexpr std::size_t OFFSET_FLAGS        = 3;
constexpr std::size_t OFFSET_VALUE        = 4;

constexpr std::size_t DELTA_TIME_BYTES = 2;

constexpr std::uint8_t SIZE_1_BYTE = 1;
constexpr std::uint8_t SIZE_2_BYTE = 2;
constexpr std::uint8_t SIZE_4_BYTE = 4;

// v2+ Get: scale rides in bits 3-4 of the single payload byte.
constexpr std::uint8_t GET_SCALE_MASK  = 0x03;
constexpr std::uint8_t GET_SCALE_SHIFT = 3;

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

// Read a `size`-byte big-endian signed field at `offset`, sign-extended
// (same scheme as SensorMultilevel / Configuration).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): offset/size named at the call site
auto readSigned(std::span<const std::uint8_t> payload, std::size_t offset, std::uint8_t size) -> std::int32_t
{
    std::uint32_t raw = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        raw = (raw << BITS_PER_BYTE) | std::uint32_t{payload[offset + i]};
    }
    if ((raw & signBit(size)) != 0U)
    {
        raw |= ~valueMask(size);
    }
    return static_cast<std::int32_t>(raw);
}
}  // namespace

auto Meter::encodeGet(std::uint8_t scale) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, METER_GET, static_cast<std::uint8_t>((scale & GET_SCALE_MASK) << GET_SCALE_SHIFT)};
}

auto Meter::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS || payload[1] != METER_REPORT)
    {
        return std::nullopt;
    }

    const std::uint8_t properties1 = payload[OFFSET_PROPERTIES1];
    const std::uint8_t flags       = payload[OFFSET_FLAGS];
    const auto size                = static_cast<std::uint8_t>(flags & FLAG_SIZE_MASK);
    if (!isValidSize(size) || payload.size() < REPORT_HEADER_BYTES + size + DELTA_TIME_BYTES)
    {
        return std::nullopt;
    }

    Report out;
    out.meterType = static_cast<std::uint8_t>(properties1 & PROP1_METER_TYPE_MASK);
    out.rateType  = static_cast<std::uint8_t>((properties1 & PROP1_RATE_TYPE_MASK) >> PROP1_RATE_TYPE_SHIFT);
    // scale is the 2 low bits from the flag byte plus a high bit carried in
    // properties1 (the v4 third scale bit).
    const auto scaleLow  = static_cast<std::uint8_t>((flags & FLAG_SCALE_MASK) >> FLAG_SCALE_SHIFT);
    const auto scaleBit2 = static_cast<std::uint8_t>((properties1 & PROP1_SCALE_BIT2_MASK) != 0 ? 1 : 0);
    out.scale            = static_cast<std::uint8_t>((scaleBit2 << 2) | scaleLow);
    out.precision        = static_cast<std::uint8_t>((flags & FLAG_PRECISION_MASK) >> FLAG_PRECISION_SHIFT);
    out.value            = readSigned(payload, OFFSET_VALUE, size);

    const std::size_t deltaOffset = OFFSET_VALUE + size;
    out.deltaTime =
        static_cast<std::uint16_t>((std::uint16_t{payload[deltaOffset]} << BITS_PER_BYTE) | payload[deltaOffset + 1]);

    if (out.deltaTime != 0)
    {
        const std::size_t prevOffset = deltaOffset + DELTA_TIME_BYTES;
        if (payload.size() < prevOffset + size)
        {
            return std::nullopt;  // deltaTime promises a previous value that isn't there
        }
        out.previousValue = readSigned(payload, prevOffset, size);
        out.hasPrevious   = true;
    }
    return out;
}

auto Meter::meterTypeName(std::uint8_t meterType) -> const char*
{
    switch (meterType)
    {
    case TYPE_ELECTRIC:
        return "electric";
    case TYPE_GAS:
        return "gas";
    case TYPE_WATER:
        return "water";
    default:
        return nullptr;
    }
}

// Scale codes are small spec-table literals (SDS13781), named at each
// return; the (meterType, scale) pair is documented at the call site.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,bugprone-easily-swappable-parameters)
auto Meter::scaleUnit(std::uint8_t meterType, std::uint8_t scale) -> const char*
{
    // Scale meaning is meterType-specific (SDS13781). Only the common v3
    // scales are named; unknown pairs fall back to "".
    switch (meterType)
    {
    case TYPE_ELECTRIC:
        switch (scale)
        {
        case 0:
            return "kWh";
        case 1:
            return "kVAh";
        case 2:
            return "W";
        case 3:
            return "pulses";
        case 4:
            return "V";
        case 5:
            return "A";
        case 6:
            return "power factor";
        default:
            return "";
        }
    case TYPE_GAS:
        switch (scale)
        {
        case 0:
            return "m^3";
        case 1:
            return "ft^3";
        case 3:
            return "pulses";
        default:
            return "";
        }
    case TYPE_WATER:
        switch (scale)
        {
        case 0:
            return "m^3";
        case 1:
            return "ft^3";
        case 2:
            return "gal";
        case 3:
            return "pulses";
        default:
            return "";
        }
    default:
        return "";
    }
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,bugprone-easily-swappable-parameters)
