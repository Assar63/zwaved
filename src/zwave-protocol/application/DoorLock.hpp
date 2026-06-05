#ifndef ZWAVED_DOOR_LOCK_HPP
#define ZWAVED_DOOR_LOCK_HPP

// IWYU pragma: begin_exports
#include "DoorLock.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Door Lock Command Class (0x62). Operation control for locks.
///
/// OPERATION_SET takes a single mode byte; OPERATION_REPORT carries the
/// current mode, door-handle mode, condition, lock-timeout, and (v4+) a
/// target mode + duration. mode: 0x00 unsecured, 0x01 unsecured w/ timeout,
/// 0x10 inside-handles, 0x11 inside-handles w/ timeout, 0xFF secured.
/// `encodeSet(mode)` / `encodeGet()` come from DoorLock.gen.hpp.
///
/// Only the OPERATION triplet is implemented; the CONFIGURATION (auto-relock)
/// triplet is not. Real locks require Security S0/S2 transport (#26/#27).
namespace DoorLock
{
struct OperationReport
{
    std::uint8_t currentMode        = 0;
    std::uint8_t handlesMode        = 0;
    std::uint8_t condition          = 0;
    std::uint8_t lockTimeoutMinutes = 0;
    std::uint8_t lockTimeoutSeconds = 0;
    std::uint8_t targetMode         = 0;  // v4+; 0 otherwise
    std::uint8_t duration           = 0;  // v4+; 0 otherwise
};

/// Decode a Door Lock Operation Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report (wrong
/// CC/cmd or shorter than the v1 5-field body).
[[nodiscard]] auto decodeOperationReport(std::span<const std::uint8_t> payload) -> std::optional<OperationReport>;
}  // namespace DoorLock

#endif  // ZWAVED_DOOR_LOCK_HPP
