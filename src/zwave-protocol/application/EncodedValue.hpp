#ifndef ZWAVED_ENCODED_VALUE_HPP
#define ZWAVED_ENCODED_VALUE_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Shared codec for the SDS13781 "precision/scale/size" encoded value that
/// several Z-Wave CCs embed identically: a single flag byte packing
/// `precision << 5 | scale << 3 | size`, followed by a signed big-endian
/// value of `size` (1, 2, or 4) bytes. Used by Sensor Multilevel (CC 0x31)
/// and Thermostat Setpoint (CC 0x43). (Meter (CC 0x32) carries an extra
/// high scale bit in a separate byte, so it keeps its own decode.)
namespace EncodedValue
{
struct Decoded
{
    std::uint8_t size      = 0;  // 1, 2, or 4
    std::uint8_t scale     = 0;  // unit selector (0..3)
    std::uint8_t precision = 0;  // decimal shift: reading = value / 10^precision
    std::int32_t value     = 0;  // raw signed, sign-extended from the size-byte field
};

/// Decode the `flags` byte plus the `valueBytes` that follow it. Returns
/// std::nullopt when the size field is not 1/2/4 or `valueBytes` is shorter
/// than the declared size.
[[nodiscard]] auto decode(std::uint8_t flags, std::span<const std::uint8_t> valueBytes) -> std::optional<Decoded>;

/// Encode `(precision, scale, value)` into a flag byte followed by the
/// signed big-endian value, choosing the smallest size (1/2/4) that holds
/// `value`. `precision` is masked to 3 bits and `scale` to 2 bits.
[[nodiscard]] auto encode(std::uint8_t precision, std::uint8_t scale, std::int32_t value) -> std::vector<std::uint8_t>;
}  // namespace EncodedValue

#endif  // ZWAVED_ENCODED_VALUE_HPP
