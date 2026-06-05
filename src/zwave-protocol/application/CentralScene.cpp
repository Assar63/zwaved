#include "CentralScene.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace
{
// Notification: CC + cmd + sequenceNumber + properties1 + sceneNumber = 5 bytes.
constexpr std::size_t NOTIFICATION_MIN_BYTES = 5;
constexpr std::size_t OFFSET_SEQUENCE        = 2;
constexpr std::size_t OFFSET_PROPERTIES      = 3;
constexpr std::size_t OFFSET_SCENE           = 4;

// Properties1: bits 0-2 keyAttribute; bit 7 slowRefresh (v2+).
constexpr std::uint8_t KEY_ATTRIBUTE_MASK = 0x07;
constexpr std::uint8_t SLOW_REFRESH_FLAG  = 0x80;
}  // namespace

auto CentralScene::decodeNotification(std::span<const std::uint8_t> payload) -> std::optional<Notification>
{
    if (payload.size() < NOTIFICATION_MIN_BYTES || payload[0] != COMMAND_CLASS ||
        payload[1] != CENTRAL_SCENE_NOTIFICATION)
    {
        return std::nullopt;
    }

    const std::uint8_t properties = payload[OFFSET_PROPERTIES];
    Notification out;
    out.sequenceNumber = payload[OFFSET_SEQUENCE];
    out.keyAttribute   = static_cast<std::uint8_t>(properties & KEY_ATTRIBUTE_MASK);
    out.sceneNumber    = payload[OFFSET_SCENE];
    out.slowRefresh    = (properties & SLOW_REFRESH_FLAG) != 0;
    return out;
}
