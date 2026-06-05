#include "ColorSwitch.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
// v1 Report: CC + cmd + componentId + value = 4 bytes. v3 appends
// targetValue + duration.
constexpr std::size_t REPORT_MIN_BYTES = 4;
constexpr std::size_t REPORT_V3_BYTES  = 6;
constexpr std::size_t OFFSET_COMPONENT = 2;
constexpr std::size_t OFFSET_VALUE     = 3;
constexpr std::size_t OFFSET_TARGET    = 4;
constexpr std::size_t OFFSET_DURATION  = 5;

constexpr std::size_t PAIR_BYTES = 2;  // (componentId, value)
}  // namespace

auto ColorSwitch::encodeGet(std::uint8_t componentId) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, SWITCH_COLOR_GET, componentId};
}

auto ColorSwitch::encodeSet(std::span<const std::uint8_t> components,
                            std::uint8_t duration) -> std::vector<std::uint8_t>
{
    const auto pairCount = static_cast<std::uint8_t>(components.size() / PAIR_BYTES);
    std::vector<std::uint8_t> out{COMMAND_CLASS, SWITCH_COLOR_SET, pairCount};
    for (std::size_t i = 0; i + PAIR_BYTES <= components.size(); i += PAIR_BYTES)
    {
        out.push_back(components[i]);
        out.push_back(components[i + 1]);
    }
    out.push_back(duration);
    return out;
}

auto ColorSwitch::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_MIN_BYTES || payload[0] != COMMAND_CLASS || payload[1] != SWITCH_COLOR_REPORT)
    {
        return std::nullopt;
    }

    Report out;
    out.componentId = payload[OFFSET_COMPONENT];
    out.value       = payload[OFFSET_VALUE];
    if (payload.size() >= REPORT_V3_BYTES)
    {
        out.targetValue = payload[OFFSET_TARGET];
        out.duration    = payload[OFFSET_DURATION];
    }
    else
    {
        out.targetValue = out.value;  // v1: no transition fields
    }
    return out;
}
