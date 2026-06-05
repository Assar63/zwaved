#include "DoorLock.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// encodeSet / encodeGet bodies live in DoorLock.gen.cpp.

namespace
{
// v1 Operation Report: CC + cmd + currentMode + handlesMode + condition
// + timeoutMinutes + timeoutSeconds = 7 bytes. v4 appends targetMode +
// duration.
constexpr std::size_t REPORT_MIN_BYTES = 7;
constexpr std::size_t REPORT_V4_BYTES  = 9;
constexpr std::size_t OFFSET_CURRENT   = 2;
constexpr std::size_t OFFSET_HANDLES   = 3;
constexpr std::size_t OFFSET_CONDITION = 4;
constexpr std::size_t OFFSET_MINUTES   = 5;
constexpr std::size_t OFFSET_SECONDS   = 6;
constexpr std::size_t OFFSET_TARGET    = 7;
constexpr std::size_t OFFSET_DURATION  = 8;
}  // namespace

auto DoorLock::decodeOperationReport(std::span<const std::uint8_t> payload) -> std::optional<OperationReport>
{
    if (payload.size() < REPORT_MIN_BYTES || payload[0] != COMMAND_CLASS || payload[1] != DOOR_LOCK_REPORT)
    {
        return std::nullopt;
    }

    OperationReport out;
    out.currentMode        = payload[OFFSET_CURRENT];
    out.handlesMode        = payload[OFFSET_HANDLES];
    out.condition          = payload[OFFSET_CONDITION];
    out.lockTimeoutMinutes = payload[OFFSET_MINUTES];
    out.lockTimeoutSeconds = payload[OFFSET_SECONDS];
    if (payload.size() >= REPORT_V4_BYTES)
    {
        out.targetMode = payload[OFFSET_TARGET];
        out.duration   = payload[OFFSET_DURATION];
    }
    return out;
}
