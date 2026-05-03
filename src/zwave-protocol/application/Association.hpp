#ifndef ZWAVED_ASSOCIATION_HPP
#define ZWAVED_ASSOCIATION_HPP

// IWYU pragma: begin_exports
#include "Association.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Association Command Class (0x85). Lets a controller manage
/// the per-group association lists on a node — i.e. which other nodes
/// the source will push unsolicited commands to (Basic SET on toggle,
/// sensor reports, etc.). Group 1 is conventionally the lifeline group
/// and is owned by the primary controller.
///
/// Constants are generated from InterfaceManifest.yml (Association.gen.hpp).
/// All encoder/decoder bodies stay hand-written here because Association's
/// wire shapes (variable-length member lists, multi-frame Reports) aren't
/// fully expressible in the manifest today.
namespace Association
{

/// Decoded ASSOCIATION_REPORT. `reportsToFollow` is non-zero when a
/// group's member list spans multiple frames; the controller should
/// concatenate members across frames until reportsToFollow == 0.
struct Report
{
    std::uint8_t groupId         = 0;
    std::uint8_t maxSupported    = 0;
    std::uint8_t reportsToFollow = 0;
    std::vector<std::uint8_t> members;
};

struct GroupingsReport
{
    std::uint8_t supportedGroupings = 0;
};

/// CC payload for ASSOCIATION_SET. Caller wraps the result in
/// FUNC_ID_ZW_SEND_DATA addressed to the target node.
[[nodiscard]] auto encodeSet(std::uint8_t groupId, std::span<const std::uint8_t> members) -> std::vector<std::uint8_t>;

[[nodiscard]] auto encodeRemove(std::uint8_t groupId,
                                std::span<const std::uint8_t> members) -> std::vector<std::uint8_t>;

[[nodiscard]] auto encodeGet(std::uint8_t groupId) -> std::vector<std::uint8_t>;

[[nodiscard]] auto encodeGroupingsGet() -> std::vector<std::uint8_t>;

[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;

[[nodiscard]] auto decodeGroupingsReport(std::span<const std::uint8_t> payload) -> std::optional<GroupingsReport>;
}  // namespace Association

#endif  // ZWAVED_ASSOCIATION_HPP
