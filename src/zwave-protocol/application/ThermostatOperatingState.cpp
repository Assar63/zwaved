#include "ThermostatOperatingState.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeGet body lives in ThermostatOperatingState.gen.cpp (empty payload).

namespace
{
// Report: CC + cmd + state = 3 bytes.
constexpr std::size_t REPORT_MIN_BYTES = 3;
constexpr std::size_t OFFSET_STATE     = 2;

// Low 4 bits of the byte are the operating state; high bits reserved.
constexpr std::uint8_t STATE_MASK = 0x0F;
}  // namespace

auto ThermostatOperatingState::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_MIN_BYTES || payload[0] != COMMAND_CLASS ||
        payload[1] != THERMOSTAT_OPERATING_STATE_REPORT)
    {
        return std::nullopt;
    }

    Report out;
    out.state = static_cast<std::uint8_t>(payload[OFFSET_STATE] & STATE_MASK);
    return out;
}
