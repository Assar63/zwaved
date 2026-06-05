#ifndef ZWAVED_USER_CODE_HPP
#define ZWAVED_USER_CODE_HPP

// IWYU pragma: begin_exports
#include "UserCode.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave User Code Command Class (0x63). Per-slot PIN management for locks.
///
/// SET writes a slot's `(userIdStatus, userCode)`; REPORT carries them back.
/// USERS_NUMBER_GET/REPORT report how many code slots the lock has.
/// userIdStatus: 0x00 available (free), 0x01 enabled/occupied, 0xFE
/// not-available. The code is raw 4–10 ASCII digits.
///
/// `encodeGet(userIdentifier)` / `encodeUsersNumberGet()` come from
/// UserCode.gen.hpp; the variable-length `encodeSet` and the decoders are
/// hand-written. Real locks require Security S0/S2 transport (#26/#27).
namespace UserCode
{
struct Report
{
    std::uint8_t userIdentifier = 0;
    std::uint8_t userIdStatus   = 0;
    std::vector<std::uint8_t> userCode;
};

struct UsersNumberReport
{
    std::uint8_t supportedUsers = 0;
};

/// Encode a User Code Set: `userIdentifier`, `userIdStatus`, then the raw
/// code bytes (4–10 ASCII digits; empty to clear a slot with status
/// available).
[[nodiscard]] auto encodeSet(std::uint8_t userIdentifier,
                             std::uint8_t userIdStatus,
                             std::span<const std::uint8_t> code) -> std::vector<std::uint8_t>;

/// Decode a User Code Report (one slot). nullopt on wrong CC/cmd or a body
/// shorter than (userIdentifier, userIdStatus).
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;

/// Decode a User Code Users-Number Report. nullopt on wrong CC/cmd or too
/// short.
[[nodiscard]] auto decodeUsersNumberReport(std::span<const std::uint8_t> payload) -> std::optional<UsersNumberReport>;
}  // namespace UserCode

#endif  // ZWAVED_USER_CODE_HPP
