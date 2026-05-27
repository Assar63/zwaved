#include "Configuration.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
constexpr unsigned BITS_PER_BYTE = 8;
constexpr unsigned BYTE_MASK     = 0xFFU;

// CONFIGURATION_GET payload is just `parameter`. CC + cmd + 1 byte.
constexpr std::size_t GET_BYTES = 3;

// CONFIGURATION_REPORT payload header (before the variable-length
// value): CC + cmd + parameter + size = 4 bytes; value follows.
constexpr std::size_t REPORT_HEADER_BYTES     = 4;
constexpr std::size_t REPORT_OFFSET_PARAMETER = 2;
constexpr std::size_t REPORT_OFFSET_SIZE      = 3;
constexpr std::size_t REPORT_OFFSET_VALUE     = 4;

// MSB shift for sign-extending a `size`-byte value into int32.
auto signBit(std::uint8_t size) -> std::uint32_t
{
    return std::uint32_t{1} << ((static_cast<unsigned>(size) * BITS_PER_BYTE) - 1U);
}

auto valueMask(std::uint8_t size) -> std::uint32_t
{
    // For size in {1, 2, 4}: 0xFF, 0xFFFF, 0xFFFFFFFF.
    // (`1U << 32` is UB; size=4 is handled by returning ~0U directly.)
    if (size == Configuration::SIZE_4_BYTE)
    {
        return ~std::uint32_t{0};
    }
    return (std::uint32_t{1} << (static_cast<unsigned>(size) * BITS_PER_BYTE)) - 1U;
}

auto isValidSize(std::uint8_t size) -> bool
{
    return size == Configuration::SIZE_1_BYTE || size == Configuration::SIZE_2_BYTE ||
           size == Configuration::SIZE_4_BYTE;
}
}  // namespace

// NOLINTBEGIN(bugprone-easily-swappable-parameters): wire shape is fixed by CONFIGURATION_SET
auto Configuration::encodeSet(std::uint8_t parameter,
                              std::uint8_t size,
                              std::int32_t value) -> std::vector<std::uint8_t>
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    if (!isValidSize(size))
    {
        // Out-of-range size — refuse rather than producing a
        // malformed frame. Caller should validate before calling.
        return {};
    }
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(REPORT_HEADER_BYTES) + size);
    out.push_back(COMMAND_CLASS);
    out.push_back(CONFIGURATION_SET);
    out.push_back(parameter);
    out.push_back(size);

    // Truncate `value` to the low `size` bytes, big-endian. Works
    // for both signed and unsigned interpretations: a -1 (signed)
    // and 0xFFFFFFFF (unsigned) both produce identical low bytes.
    const auto bits = static_cast<unsigned>(value);
    for (std::size_t i = 0; i < size; ++i)
    {
        const unsigned shift = (static_cast<unsigned>(size) - 1U - static_cast<unsigned>(i)) * BITS_PER_BYTE;
        out.push_back(static_cast<std::uint8_t>((bits >> shift) & BYTE_MASK));
    }
    return out;
}

auto Configuration::encodeGet(std::uint8_t parameter) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, CONFIGURATION_GET, parameter};
}

auto Configuration::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS || payload[1] != CONFIGURATION_REPORT)
    {
        return std::nullopt;
    }
    Report out;
    out.parameter = payload[REPORT_OFFSET_PARAMETER];
    out.size      = payload[REPORT_OFFSET_SIZE];
    if (!isValidSize(out.size))
    {
        return std::nullopt;
    }
    if (payload.size() < static_cast<std::size_t>(REPORT_HEADER_BYTES) + out.size)
    {
        return std::nullopt;
    }

    // Read `size` big-endian bytes into a u32, then sign-extend
    // from the field's MSB. For size=4 the result is just the raw
    // bits reinterpreted as i32; for size=1/2 we manually
    // sign-extend so a 0xFF / 0xFFFF in the wire bytes becomes
    // -1 in the int32 result.
    std::uint32_t raw = 0;
    for (std::size_t i = 0; i < out.size; ++i)
    {
        raw = (raw << BITS_PER_BYTE) | std::uint32_t{payload[REPORT_OFFSET_VALUE + i]};
    }
    if ((raw & signBit(out.size)) != 0U)
    {
        raw |= ~valueMask(out.size);
    }
    out.value = static_cast<std::int32_t>(raw);
    return out;
}
