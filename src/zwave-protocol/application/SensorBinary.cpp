#include "SensorBinary.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeGet body lives in SensorBinary.gen.cpp (empty v1 payload).

namespace
{
// v1 Report: CC + cmd + value = 3 bytes. v2 appends a sensorType byte.
constexpr std::size_t REPORT_MIN_BYTES   = 3;
constexpr std::size_t OFFSET_VALUE       = 2;
constexpr std::size_t OFFSET_SENSOR_TYPE = 3;
}  // namespace

auto SensorBinary::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_MIN_BYTES || payload[0] != COMMAND_CLASS || payload[1] != SENSOR_BINARY_REPORT)
    {
        return std::nullopt;
    }

    Report out;
    out.value = payload[OFFSET_VALUE];
    // v2+ carries a trailing sensorType byte; v1 leaves it implicitly 0.
    if (payload.size() > OFFSET_SENSOR_TYPE)
    {
        out.sensorType = payload[OFFSET_SENSOR_TYPE];
    }
    return out;
}
