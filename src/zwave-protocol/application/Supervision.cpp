#include "Supervision.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
// CC + cmd + properties + status + duration.
constexpr std::size_t REPORT_BYTES    = 5;
constexpr std::size_t OFFSET_PROPS    = 2;
constexpr std::size_t OFFSET_STATUS   = 3;
constexpr std::size_t OFFSET_DURATION = 4;
}  // namespace

auto Supervision::encodeGet(std::uint8_t sessionId,
                            bool requestUpdates,
                            std::span<const std::uint8_t> innerCommand) -> std::vector<std::uint8_t>
{
    const auto properties =
        static_cast<std::uint8_t>((requestUpdates ? MORE_UPDATES_FLAG : 0) | (sessionId & SESSION_ID_MASK));
    std::vector<std::uint8_t> out;
    out.reserve(4 + innerCommand.size());
    out.push_back(COMMAND_CLASS);
    out.push_back(SUPERVISION_GET);
    out.push_back(properties);
    out.push_back(static_cast<std::uint8_t>(innerCommand.size()));
    out.insert(out.end(), innerCommand.begin(), innerCommand.end());
    return out;
}

auto Supervision::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_BYTES || payload[0] != COMMAND_CLASS || payload[1] != SUPERVISION_REPORT)
    {
        return std::nullopt;
    }
    const std::uint8_t properties = payload[OFFSET_PROPS];
    return Report{
        .sessionId         = static_cast<std::uint8_t>(properties & SESSION_ID_MASK),
        .moreStatusUpdates = (properties & MORE_UPDATES_FLAG) != 0,
        .status            = payload[OFFSET_STATUS],
        .duration          = payload[OFFSET_DURATION],
    };
}
