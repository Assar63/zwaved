#include "ThermostatMode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeSet / encodeGet bodies live in ThermostatMode.gen.cpp.

namespace
{
// Report: CC + cmd + mode = 3 bytes.
constexpr std::size_t REPORT_MIN_BYTES = 3;
constexpr std::size_t OFFSET_MODE      = 2;

// Low 5 bits of the mode byte are the mode; the high 3 bits are a v3
// manufacturer-data-field count we don't surface.
constexpr std::uint8_t MODE_MASK = 0x1F;
}  // namespace

auto ThermostatMode::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_MIN_BYTES || payload[0] != COMMAND_CLASS || payload[1] != THERMOSTAT_MODE_REPORT)
    {
        return std::nullopt;
    }

    Report out;
    out.mode = static_cast<std::uint8_t>(payload[OFFSET_MODE] & MODE_MASK);
    return out;
}
