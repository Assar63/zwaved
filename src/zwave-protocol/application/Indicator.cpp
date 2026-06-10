#include "Indicator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeSet / encodeGet bodies live in Indicator.gen.cpp.

namespace
{
// CC byte + cmd byte + value.
constexpr std::size_t REPORT_BYTES = 3;
}  // namespace

auto Indicator::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_BYTES || payload[0] != COMMAND_CLASS || payload[1] != INDICATOR_REPORT)
    {
        return std::nullopt;
    }
    return Report{.value = payload[2]};
}
