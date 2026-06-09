#include "SceneActivation.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace
{
// CC byte + cmd byte + sceneId. The dimming-duration byte is optional on
// the wire (v1 omits it); default it to 0 (instant) when absent.
constexpr std::size_t SET_MIN_BYTES   = 3;
constexpr std::size_t OFFSET_SCENE_ID = 2;
constexpr std::size_t OFFSET_DURATION = 3;
}  // namespace

auto SceneActivation::decodeSet(std::span<const std::uint8_t> payload) -> std::optional<Set>
{
    if (payload.size() < SET_MIN_BYTES || payload[0] != COMMAND_CLASS || payload[1] != SCENE_ACTIVATION_SET)
    {
        return std::nullopt;
    }
    Set out;
    out.sceneId = payload[OFFSET_SCENE_ID];
    if (payload.size() > OFFSET_DURATION)
    {
        out.dimmingDuration = payload[OFFSET_DURATION];
    }
    return out;
}
