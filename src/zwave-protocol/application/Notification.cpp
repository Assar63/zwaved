#include "Notification.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
// v3 Report header before the variable-length parameters:
//   CC + cmd + v1AlarmType + v1AlarmLevel + reserved + notificationStatus
//   + notificationType + event + properties(paramLength) = 9 bytes.
constexpr std::size_t REPORT_HEADER_BYTES = 9;
constexpr std::size_t OFFSET_STATUS       = 5;
constexpr std::size_t OFFSET_TYPE         = 6;
constexpr std::size_t OFFSET_EVENT        = 7;
constexpr std::size_t OFFSET_PARAM_LEN    = 8;
constexpr std::size_t OFFSET_PARAMS       = 9;

// Properties1 byte: bits 0-4 are the event-parameter length; bit 7 (a
// trailing sequence number) is ignored.
constexpr std::uint8_t PARAM_LENGTH_MASK = 0x1F;

// v1 alarm type + event filter sent as 0 in a v3 type-scoped Get.
constexpr std::uint8_t GET_V1_ALARM_TYPE = 0x00;
constexpr std::uint8_t GET_EVENT_ANY     = 0x00;
}  // namespace

auto Notification::encodeGet(std::uint8_t notificationType) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, NOTIFICATION_GET, GET_V1_ALARM_TYPE, notificationType, GET_EVENT_ANY};
}

auto Notification::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS || payload[1] != NOTIFICATION_REPORT)
    {
        return std::nullopt;
    }
    const auto paramLength = static_cast<std::size_t>(payload[OFFSET_PARAM_LEN] & PARAM_LENGTH_MASK);
    if (payload.size() < REPORT_HEADER_BYTES + paramLength)
    {
        return std::nullopt;
    }
    Report out;
    out.status           = payload[OFFSET_STATUS];
    out.notificationType = payload[OFFSET_TYPE];
    out.event            = payload[OFFSET_EVENT];
    const auto params    = payload.subspan(OFFSET_PARAMS, paramLength);
    out.parameters.assign(params.begin(), params.end());
    return out;
}
