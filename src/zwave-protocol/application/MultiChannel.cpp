#include "MultiChannel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
// CC + cmd + sourceEndpoint + destinationEndpoint, then >= 1 inner byte.
constexpr std::size_t ENCAP_HEADER_BYTES = 4;
constexpr std::size_t OFFSET_SOURCE_EP   = 2;
constexpr std::size_t OFFSET_DEST_EP     = 3;
constexpr std::size_t OFFSET_INNER       = 4;
}  // namespace

auto MultiChannel::decodeEncap(std::span<const std::uint8_t> payload) -> std::optional<Encap>
{
    // Need the 4-byte header plus at least one inner command byte.
    if (payload.size() <= ENCAP_HEADER_BYTES || payload[0] != COMMAND_CLASS || payload[1] != MULTI_CHANNEL_ENCAP)
    {
        return std::nullopt;
    }
    Encap out;
    out.sourceEndpoint      = static_cast<std::uint8_t>(payload[OFFSET_SOURCE_EP] & ENDPOINT_MASK);
    out.destinationEndpoint = static_cast<std::uint8_t>(payload[OFFSET_DEST_EP] & ENDPOINT_MASK);
    out.bitAddress          = (payload[OFFSET_DEST_EP] & BIT_ADDRESS_FLAG) != 0;
    out.innerCommand.assign(payload.begin() + OFFSET_INNER, payload.end());
    return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): endpoints are clearly named at call sites
auto MultiChannel::encodeEncap(std::uint8_t sourceEndpoint,
                               std::uint8_t destinationEndpoint,
                               std::span<const std::uint8_t> innerCommand) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    out.reserve(ENCAP_HEADER_BYTES + innerCommand.size());
    out.push_back(COMMAND_CLASS);
    out.push_back(MULTI_CHANNEL_ENCAP);
    out.push_back(static_cast<std::uint8_t>(sourceEndpoint & ENDPOINT_MASK));
    out.push_back(static_cast<std::uint8_t>(destinationEndpoint & ENDPOINT_MASK));
    out.insert(out.end(), innerCommand.begin(), innerCommand.end());
    return out;
}
