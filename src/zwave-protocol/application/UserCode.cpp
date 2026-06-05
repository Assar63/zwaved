#include "UserCode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// encodeGet / encodeUsersNumberGet bodies live in UserCode.gen.cpp.

namespace
{
// Report: CC + cmd + userIdentifier + userIdStatus = 4 bytes; the code
// bytes follow.
constexpr std::size_t REPORT_MIN_BYTES = 4;
constexpr std::size_t OFFSET_USER_ID   = 2;
constexpr std::size_t OFFSET_STATUS    = 3;
constexpr std::size_t OFFSET_CODE      = 4;

// Users-Number Report: CC + cmd + supportedUsers = 3 bytes.
constexpr std::size_t USERS_NUMBER_MIN_BYTES = 3;
constexpr std::size_t OFFSET_SUPPORTED_USERS = 2;
}  // namespace

auto UserCode::encodeSet(std::uint8_t userIdentifier,
                         std::uint8_t userIdStatus,
                         std::span<const std::uint8_t> code) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{COMMAND_CLASS, USER_CODE_SET, userIdentifier, userIdStatus};
    out.insert(out.end(), code.begin(), code.end());
    return out;
}

auto UserCode::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_MIN_BYTES || payload[0] != COMMAND_CLASS || payload[1] != USER_CODE_REPORT)
    {
        return std::nullopt;
    }
    const auto code = payload.subspan(OFFSET_CODE);
    Report out;
    out.userIdentifier = payload[OFFSET_USER_ID];
    out.userIdStatus   = payload[OFFSET_STATUS];
    out.userCode.assign(code.begin(), code.end());
    return out;
}

auto UserCode::decodeUsersNumberReport(std::span<const std::uint8_t> payload) -> std::optional<UsersNumberReport>
{
    if (payload.size() < USERS_NUMBER_MIN_BYTES || payload[0] != COMMAND_CLASS ||
        payload[1] != USER_CODE_USERS_NUMBER_REPORT)
    {
        return std::nullopt;
    }
    UsersNumberReport out;
    out.supportedUsers = payload[OFFSET_SUPPORTED_USERS];
    return out;
}
