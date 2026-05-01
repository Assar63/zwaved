#include "MultichannelAssociation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

namespace
{
// CC byte + cmd byte + groupId + maxSupported + reportsToFollow.
constexpr std::size_t REPORT_HEADER_BYTES = 5;
// CC byte + cmd byte + supportedGroupings.
constexpr std::size_t GROUPINGS_REPORT_HEADER_BYTES = 3;

auto encodeMembers(std::uint8_t command,
                   std::uint8_t groupId,
                   std::span<const std::uint8_t> nodeMembers,
                   std::span<const MultichannelAssociation::EndpointMember> endpointMembers,
                   bool emitMarker) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> result;
    result.reserve(3 + nodeMembers.size() + (emitMarker ? 1 : 0) + (endpointMembers.size() * 2));
    result.push_back(MultichannelAssociation::COMMAND_CLASS);
    result.push_back(command);
    result.push_back(groupId);
    for (const auto nodeId : nodeMembers)
    {
        result.push_back(nodeId);
    }
    if (emitMarker)
    {
        result.push_back(MultichannelAssociation::MARKER);
        for (const auto& member : endpointMembers)
        {
            result.push_back(member.nodeId);
            result.push_back(member.endpoint);
        }
    }
    return result;
}
}  // namespace

auto MultichannelAssociation::encodeSet(std::uint8_t groupId,
                                        std::span<const std::uint8_t> nodeMembers,
                                        std::span<const EndpointMember> endpointMembers) -> std::vector<std::uint8_t>
{
    // SET always emits the MARKER even when endpointMembers is empty —
    // the receiver then sees an explicit empty endpoint segment, which
    // is unambiguous.
    return encodeMembers(MULTI_CHANNEL_ASSOCIATION_SET, groupId, nodeMembers, endpointMembers, /*emitMarker=*/true);
}

auto MultichannelAssociation::encodeRemove(std::uint8_t groupId,
                                           std::span<const std::uint8_t> nodeMembers,
                                           std::span<const EndpointMember> endpointMembers) -> std::vector<std::uint8_t>
{
    // Spec: a REMOVE with no MARKER and no node members means
    // "remove every member of this group". Mirrors the simpler
    // Association::encodeRemove behaviour for ergonomic parity.
    const bool removeAll  = nodeMembers.empty() && endpointMembers.empty();
    const bool emitMarker = !removeAll;
    return encodeMembers(MULTI_CHANNEL_ASSOCIATION_REMOVE, groupId, nodeMembers, endpointMembers, emitMarker);
}

auto MultichannelAssociation::encodeGet(std::uint8_t groupId) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, MULTI_CHANNEL_ASSOCIATION_GET, groupId};
}

auto MultichannelAssociation::encodeGroupingsGet() -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, MULTI_CHANNEL_ASSOCIATION_GROUPINGS_GET};
}

auto MultichannelAssociation::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS ||
        payload[1] != MULTI_CHANNEL_ASSOCIATION_REPORT)
    {
        return std::nullopt;
    }
    Report out;
    out.groupId         = payload[2];
    out.maxSupported    = payload[3];
    out.reportsToFollow = payload[4];

    const auto remaining = payload.subspan(REPORT_HEADER_BYTES);
    const auto markerIt  = std::find(remaining.begin(), remaining.end(), MARKER);

    out.nodeMembers.assign(remaining.begin(), markerIt);

    if (markerIt != remaining.end())
    {
        const auto endpointStart = markerIt + 1;
        const auto endpointBytes = static_cast<std::size_t>(std::distance(endpointStart, remaining.end()));
        // Endpoint segment is a sequence of (nodeId, endpoint) pairs.
        // An odd byte count means the last pair is truncated — treat
        // as malformed.
        if (endpointBytes % 2 != 0)
        {
            return std::nullopt;
        }
        out.endpointMembers.reserve(endpointBytes / 2);
        for (auto iter = endpointStart; iter != remaining.end(); iter += 2)
        {
            out.endpointMembers.push_back(EndpointMember{.nodeId = *iter, .endpoint = *(iter + 1)});
        }
    }
    return out;
}

auto MultichannelAssociation::decodeGroupingsReport(std::span<const std::uint8_t> payload)
    -> std::optional<GroupingsReport>
{
    if (payload.size() < GROUPINGS_REPORT_HEADER_BYTES || payload[0] != COMMAND_CLASS ||
        payload[1] != MULTI_CHANNEL_ASSOCIATION_GROUPINGS_REPORT)
    {
        return std::nullopt;
    }
    return GroupingsReport{.supportedGroupings = payload[2]};
}
