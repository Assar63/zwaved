#ifndef ZWAVED_SUPERVISION_HPP
#define ZWAVED_SUPERVISION_HPP

// IWYU pragma: begin_exports
#include "Supervision.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Supervision Command Class (0x6C) — explicit-acknowledgement
/// encapsulation (#14). SUPERVISION_GET wraps an outbound CC frame with a
/// 6-bit session id; the node replies with SUPERVISION_REPORT whose
/// `status` distinguishes *transmitted* from *applied*. Constants come from
/// Supervision.gen.hpp; the GET encapsulation + Report decode are
/// hand-written (dynamic inner payload).
namespace Supervision
{
/// Decoded Supervision Report. `sessionId` echoes the GET's nonce so the
/// caller correlates; `status` is one of STATUS_*; `moreStatusUpdates`
/// means a final report follows after `duration`.
struct Report
{
    std::uint8_t sessionId = 0;
    bool moreStatusUpdates = false;
    std::uint8_t status    = 0;
    std::uint8_t duration  = 0;
};

/// Encapsulate `innerCommand` (a complete CC frame) in a SUPERVISION_GET
/// for `sessionId` (low 6 bits used). `requestUpdates` sets the
/// more-status-updates flag, asking the node for interim Working reports
/// before the final one.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): sessionId / requestUpdates are clearly named at call sites
[[nodiscard]] auto encodeGet(std::uint8_t sessionId,
                             bool requestUpdates,
                             std::span<const std::uint8_t> innerCommand) -> std::vector<std::uint8_t>;

/// Decode a Supervision Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Report.
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace Supervision

#endif  // ZWAVED_SUPERVISION_HPP
