#include "ThermostatFanMode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeSet / encodeGet bodies live in ThermostatFanMode.gen.cpp.

namespace
{
// Report: CC + cmd + properties = 3 bytes.
constexpr std::size_t REPORT_MIN_BYTES  = 3;
constexpr std::size_t OFFSET_PROPERTIES = 2;
}  // namespace

auto ThermostatFanMode::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_MIN_BYTES || payload[0] != COMMAND_CLASS || payload[1] != THERMOSTAT_FAN_MODE_REPORT)
    {
        return std::nullopt;
    }

    const std::uint8_t properties = payload[OFFSET_PROPERTIES];
    Report out;
    out.mode = static_cast<std::uint8_t>(properties & MODE_MASK);
    out.off  = (properties & OFF_FLAG) != 0;
    return out;
}
