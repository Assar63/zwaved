#include "SensorMultilevel.hpp"

#include "EncodedValue.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeGet body lives in SensorMultilevel.gen.cpp (empty v1 payload).

namespace
{
// Report header before the variable-length value: CC + cmd + sensorType
// + flag byte = 4 bytes; the signed value follows. The precision/scale/
// size flag + value decode is shared with Thermostat Setpoint via
// EncodedValue.
constexpr std::size_t REPORT_HEADER_BYTES = 4;
constexpr std::size_t OFFSET_SENSOR_TYPE  = 2;
constexpr std::size_t OFFSET_FLAGS        = 3;
constexpr std::size_t OFFSET_VALUE        = 4;
}  // namespace

auto SensorMultilevel::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS || payload[1] != SENSOR_MULTILEVEL_REPORT)
    {
        return std::nullopt;
    }
    const auto decoded = EncodedValue::decode(payload[OFFSET_FLAGS], payload.subspan(OFFSET_VALUE));
    if (!decoded.has_value())
    {
        return std::nullopt;
    }

    Report out;
    out.sensorType = payload[OFFSET_SENSOR_TYPE];
    out.scale      = decoded->scale;
    out.precision  = decoded->precision;
    out.value      = decoded->value;
    return out;
}
