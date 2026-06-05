#ifndef ZWAVED_CENTRAL_SCENE_HPP
#define ZWAVED_CENTRAL_SCENE_HPP

// IWYU pragma: begin_exports
#include "CentralScene.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Central Scene Command Class (0x5B). Push-only scene events from
/// physical button presses on remotes and wall switches (single / double /
/// triple tap, hold, release). The daemon never sends Central Scene frames;
/// it only decodes inbound NOTIFICATION frames.
///
/// keyAttribute: 0 press 1×, 1 release, 2 hold, 3 press 2×, 4 press 3×,
/// 5 press 4×, 6 press 5×. `sequenceNumber` is a rolling counter for
/// de-duplication. Constants come from CentralScene.gen.hpp.
///
/// Only the v1+ NOTIFICATION is decoded; SUPPORTED_GET/REPORT and the v3+
/// Configuration triplet are not implemented.
namespace CentralScene
{
struct Notification
{
    std::uint8_t sequenceNumber = 0;
    std::uint8_t keyAttribute   = 0;
    std::uint8_t sceneNumber    = 0;
    bool slowRefresh            = false;
};

/// Decode a Central Scene Notification payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Notification
/// (wrong CC/cmd or too short).
[[nodiscard]] auto decodeNotification(std::span<const std::uint8_t> payload) -> std::optional<Notification>;
}  // namespace CentralScene

#endif  // ZWAVED_CENTRAL_SCENE_HPP
