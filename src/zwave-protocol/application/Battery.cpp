#include "Battery.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeGet body lives in Battery.gen.cpp.

namespace
{
// CC byte + cmd byte + level byte. v2 appends bitfields; we ignore
// them, so the minimum-size check is the only thing that matters.
constexpr std::size_t REPORT_MIN_BYTES = 3;
}  // namespace

auto Battery::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_MIN_BYTES || payload[0] != COMMAND_CLASS || payload[1] != BATTERY_REPORT)
    {
        return std::nullopt;
    }
    Report out;
    out.level      = payload[2];
    out.lowBattery = (out.level == LEVEL_LOW_WARNING);
    return out;
}
