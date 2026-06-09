#ifndef ZWAVED_SCENE_ACTIVATION_HPP
#define ZWAVED_SCENE_ACTIVATION_HPP

// IWYU pragma: begin_exports
#include "SceneActivation.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Scene Activation Command Class (0x2B). Push-only: scene
/// controllers announce "scene N is now active" via SCENE_ACTIVATION_SET.
/// The daemon never sends Scene Activation frames; it only decodes the
/// inbound SET into a typed signal the SceneOrchestrator (#124) keys
/// triggers on. Constants come from SceneActivation.gen.hpp.
namespace SceneActivation
{
/// Decoded Scene Activation Set payload. `sceneId` is the activated scene
/// number (1..255; 0 means "no scene active"). `dimmingDuration` is the
/// requested transition time: 0x00 instant, 0x01..0x7F seconds,
/// 0x80..0xFE minutes, 0xFF factory default.
struct Set
{
    std::uint8_t sceneId         = 0;
    std::uint8_t dimmingDuration = 0;
};

/// Decode a Scene Activation Set payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a well-formed Set.
[[nodiscard]] auto decodeSet(std::span<const std::uint8_t> payload) -> std::optional<Set>;
}  // namespace SceneActivation

#endif  // ZWAVED_SCENE_ACTIVATION_HPP
