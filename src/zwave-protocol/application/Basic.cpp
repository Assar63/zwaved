#include "Basic.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
// CC byte + cmd byte + currentValue.
constexpr std::size_t REPORT_V1_BYTES = 3;
// v2 adds targetValue + duration after currentValue.
constexpr std::size_t REPORT_V2_BYTES = 5;
}  // namespace

auto Basic::encodeSet(uint8_t value) -> std::vector<uint8_t>
{
    return {COMMAND_CLASS, BASIC_SET, value};
}

auto Basic::encodeGet() -> std::vector<uint8_t>
{
    return {COMMAND_CLASS, BASIC_GET};
}

auto Basic::decodeReport(std::span<const uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_V1_BYTES || payload[0] != COMMAND_CLASS || payload[1] != BASIC_REPORT)
    {
        return std::nullopt;
    }
    Report out;
    out.currentValue = payload[2];
    if (payload.size() >= REPORT_V2_BYTES)
    {
        out.targetValue  = payload[3];
        out.duration     = payload[4];
        out.wireFormatV2 = true;
    }
    else
    {
        // v1: target value isn't on the wire; mirror current so a
        // caller that only inspects targetValue still sees a sensible
        // value, while wireFormatV2 = false flags that no transition
        // information is available.
        out.targetValue = out.currentValue;
    }
    return out;
}
