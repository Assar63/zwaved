#include "BinarySwitch.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

auto BinarySwitch::encodeSet(bool turnOn) -> std::vector<uint8_t>
{
    return {COMMAND_CLASS, SWITCH_BINARY_SET, turnOn ? VALUE_ON : VALUE_OFF};
}

auto BinarySwitch::encodeGet() -> std::vector<uint8_t>
{
    return {COMMAND_CLASS, SWITCH_BINARY_GET};
}

auto BinarySwitch::decodeReport(std::span<const uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < 3 || payload[0] != COMMAND_CLASS || payload[1] != SWITCH_BINARY_REPORT)
    {
        return std::nullopt;
    }
    Report out;
    out.rawValue = payload[2];
    if (out.rawValue == VALUE_OFF)
    {
        out.state = State::Off;
    }
    else if (out.rawValue == VALUE_UNKNOWN)
    {
        out.state = State::Unknown;
    }
    else
    {
        // Per spec §2.2.20.3 Table 2.107, any non-zero value other than
        // 0xFE represents the On state.
        out.state = State::On;
    }
    return out;
}
