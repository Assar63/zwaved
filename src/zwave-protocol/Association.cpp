#include "Association.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
// CC byte + cmd byte + groupId + maxSupported + reportsToFollow.
constexpr std::size_t REPORT_HEADER_BYTES = 5;
// CC byte + cmd byte + supportedGroupings.
constexpr std::size_t GROUPINGS_REPORT_HEADER_BYTES = 3;

auto encodeMemberList(std::uint8_t command,
                      std::uint8_t groupId,
                      std::span<const std::uint8_t> members) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> result;
    result.reserve(3 + members.size());
    result.push_back(Association::COMMAND_CLASS);
    result.push_back(command);
    result.push_back(groupId);
    for (const auto nodeId : members)
    {
        result.push_back(nodeId);
    }
    return result;
}
}  // namespace

auto Association::encodeSet(std::uint8_t groupId, std::span<const std::uint8_t> members) -> std::vector<std::uint8_t>
{
    return encodeMemberList(ASSOCIATION_SET, groupId, members);
}

auto Association::encodeRemove(std::uint8_t groupId, std::span<const std::uint8_t> members) -> std::vector<std::uint8_t>
{
    // Per spec: a Remove with empty members removes ALL members from the group.
    return encodeMemberList(ASSOCIATION_REMOVE, groupId, members);
}

auto Association::encodeGet(std::uint8_t groupId) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, ASSOCIATION_GET, groupId};
}

auto Association::encodeGroupingsGet() -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, ASSOCIATION_GROUPINGS_GET};
}

auto Association::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS || payload[1] != ASSOCIATION_REPORT)
    {
        return std::nullopt;
    }
    Report out;
    out.groupId         = payload[2];
    out.maxSupported    = payload[3];
    out.reportsToFollow = payload[4];
    if (payload.size() > REPORT_HEADER_BYTES)
    {
        out.members.assign(payload.begin() + REPORT_HEADER_BYTES, payload.end());
    }
    return out;
}

auto Association::decodeGroupingsReport(std::span<const std::uint8_t> payload) -> std::optional<GroupingsReport>
{
    if (payload.size() < GROUPINGS_REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS ||
        payload[1] != ASSOCIATION_GROUPINGS_REPORT)
    {
        return std::nullopt;
    }
    return GroupingsReport{.supportedGroupings = payload[2]};
}
