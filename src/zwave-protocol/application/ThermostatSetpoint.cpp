#include "ThermostatSetpoint.hpp"

#include "EncodedValue.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
// SET/REPORT header: CC + cmd + setpointType + flag byte = 4 bytes; the
// signed value follows.
constexpr std::size_t REPORT_HEADER_BYTES  = 4;
constexpr std::size_t OFFSET_SETPOINT_TYPE = 2;
constexpr std::size_t OFFSET_FLAGS         = 3;
constexpr std::size_t OFFSET_VALUE         = 4;

// setpointType occupies the low 4 bits of its byte (high bits reserved).
constexpr std::uint8_t SETPOINT_TYPE_MASK = 0x0F;
}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): all fields named at the call site
auto ThermostatSetpoint::encodeSet(std::uint8_t setpointType,
                                   std::uint8_t precision,
                                   std::uint8_t scale,
                                   std::int32_t value) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{
        COMMAND_CLASS, THERMOSTAT_SETPOINT_SET, static_cast<std::uint8_t>(setpointType & SETPOINT_TYPE_MASK)};
    const auto encoded = EncodedValue::encode(precision, scale, value);
    out.insert(out.end(), encoded.begin(), encoded.end());
    return out;
}

auto ThermostatSetpoint::encodeGet(std::uint8_t setpointType) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, THERMOSTAT_SETPOINT_GET, static_cast<std::uint8_t>(setpointType & SETPOINT_TYPE_MASK)};
}

auto ThermostatSetpoint::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS || payload[1] != THERMOSTAT_SETPOINT_REPORT)
    {
        return std::nullopt;
    }
    const auto decoded = EncodedValue::decode(payload[OFFSET_FLAGS], payload.subspan(OFFSET_VALUE));
    if (!decoded.has_value())
    {
        return std::nullopt;
    }

    Report out;
    out.setpointType = static_cast<std::uint8_t>(payload[OFFSET_SETPOINT_TYPE] & SETPOINT_TYPE_MASK);
    out.scale        = decoded->scale;
    out.precision    = decoded->precision;
    out.value        = decoded->value;
    return out;
}
