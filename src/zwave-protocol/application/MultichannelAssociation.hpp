#ifndef ZWAVED_MULTICHANNEL_ASSOCIATION_HPP
#define ZWAVED_MULTICHANNEL_ASSOCIATION_HPP

// IWYU pragma: begin_exports
#include "MultichannelAssociation.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Multi Channel Association Command Class (0x8E). Like
/// `Association` (0x85) but lets each association group hold both
/// plain node members and node:endpoint members, so a single trigger
/// can drive a specific endpoint of a multi-channel device (e.g.
/// "the second relay channel of node 11" rather than "node 11").
///
/// The wire format places node members first, then a `MARKER`
/// (`0x00`) byte, then `(nodeId, endpoint)` pairs:
///
///     0x8E cmd groupId nodeId* MARKER (nodeId endpoint)*
///
/// On `REMOVE`, an entirely empty payload after `groupId` (no node
/// members, no MARKER) means "remove every member of the group" —
/// matching the simpler `Association::encodeRemove` semantics. With
/// any explicit members, the encoder always emits the MARKER, even
/// when only one half of the list is populated.
///
/// Constants live in MultichannelAssociation.gen.hpp; encoder /
/// decoder bodies stay hand-written here.
namespace MultichannelAssociation
{
/// One node:endpoint member of an association group. `endpoint` is a
/// 7-bit value (1..127) per Multi Channel CC; the high bit is reserved
/// and not interpreted here.
struct EndpointMember
{
    std::uint8_t nodeId   = 0;
    std::uint8_t endpoint = 0;
};

/// Decoded `MULTI_CHANNEL_ASSOCIATION_REPORT`. Multi-frame reports
/// are signalled via `reportsToFollow > 0`; the controller should
/// concatenate `nodeMembers` and `endpointMembers` across frames
/// until it sees `reportsToFollow == 0`.
struct Report
{
    std::uint8_t groupId         = 0;
    std::uint8_t maxSupported    = 0;
    std::uint8_t reportsToFollow = 0;
    std::vector<std::uint8_t> nodeMembers;
    std::vector<EndpointMember> endpointMembers;
};

struct GroupingsReport
{
    std::uint8_t supportedGroupings = 0;
};

/// CC payload for `SET`. Both lists may be empty; the encoder always
/// emits the MARKER so the receiver sees an explicit (possibly empty)
/// endpoint segment.
[[nodiscard]] auto encodeSet(std::uint8_t groupId,
                             std::span<const std::uint8_t> nodeMembers,
                             std::span<const EndpointMember> endpointMembers) -> std::vector<std::uint8_t>;

/// CC payload for `REMOVE`. A call with both `nodeMembers` and
/// `endpointMembers` empty produces a payload **without** the MARKER
/// — per spec that means "remove all members from the group",
/// mirroring `Association::encodeRemove`.
[[nodiscard]] auto encodeRemove(std::uint8_t groupId,
                                std::span<const std::uint8_t> nodeMembers,
                                std::span<const EndpointMember> endpointMembers) -> std::vector<std::uint8_t>;

[[nodiscard]] auto encodeGet(std::uint8_t groupId) -> std::vector<std::uint8_t>;

[[nodiscard]] auto encodeGroupingsGet() -> std::vector<std::uint8_t>;

[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;

[[nodiscard]] auto decodeGroupingsReport(std::span<const std::uint8_t> payload) -> std::optional<GroupingsReport>;
}  // namespace MultichannelAssociation

#endif  // ZWAVED_MULTICHANNEL_ASSOCIATION_HPP
