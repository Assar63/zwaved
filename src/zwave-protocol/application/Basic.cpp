#include "Basic.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeSet / encodeGet bodies live in Basic.gen.cpp.

namespace
{
// CC byte + cmd byte + currentValue.
constexpr std::size_t REPORT_V1_BYTES = 3;
// v2 adds targetValue + duration after currentValue.
constexpr std::size_t REPORT_V2_BYTES = 5;
// CC byte + cmd byte + value.
constexpr std::size_t SET_BYTES = 3;
}  // namespace

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

auto Basic::decodeSet(std::span<const uint8_t> payload) -> std::optional<Set>
{
    if (payload.size() < SET_BYTES || payload[0] != COMMAND_CLASS || payload[1] != BASIC_SET)
    {
        return std::nullopt;
    }
    Set out;
    out.value = payload[2];
    return out;
}
