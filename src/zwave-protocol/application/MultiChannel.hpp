#ifndef ZWAVED_MULTI_CHANNEL_HPP
#define ZWAVED_MULTI_CHANNEL_HPP

// IWYU pragma: begin_exports
#include "MultiChannel.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Multi Channel Command Class (0x60) — the encapsulation layer
/// (E1 Tier 2, #144). A node that presents several addressable endpoints
/// wraps the real command in MULTI_CHANNEL_CMD_ENCAP, carrying a source and
/// destination endpoint around the inner CC frame. The daemon decaps inbound
/// frames (routing them to a logical endpoint, #146) and encaps outbound
/// endpoint-addressed frames. Only the v2+ ENCAP (0x0D) form is handled.
/// Constants come from MultiChannel.gen.hpp.
namespace MultiChannel
{
/// Decoded MULTI_CHANNEL_CMD_ENCAP payload. `innerCommand` is the wrapped
/// CC frame (starting with its own COMMAND_CLASS byte). `bitAddress` is set
/// when the destination byte's high bit marks a multicast to a bitmask of
/// endpoints rather than a single endpoint.
struct Encap
{
    std::uint8_t sourceEndpoint      = 0;
    std::uint8_t destinationEndpoint = 0;
    bool bitAddress                  = false;
    std::vector<std::uint8_t> innerCommand;
};

/// Decode a MULTI_CHANNEL_CMD_ENCAP payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS). Returns
/// std::nullopt if the bytes are not a well-formed encap (wrong CC/cmd or no
/// inner command).
[[nodiscard]] auto decodeEncap(std::span<const std::uint8_t> payload) -> std::optional<Encap>;

/// Encapsulate `innerCommand` (a complete CC frame) for delivery from
/// `sourceEndpoint` to `destinationEndpoint`. Mirrors decodeEncap.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): endpoints are clearly named at call sites
[[nodiscard]] auto encodeEncap(std::uint8_t sourceEndpoint,
                               std::uint8_t destinationEndpoint,
                               std::span<const std::uint8_t> innerCommand) -> std::vector<std::uint8_t>;

// ---- Endpoint discovery (#13, controller side) -----------------------

/// Decoded MULTI_CHANNEL_END_POINT_REPORT — how many endpoints the node
/// presents. `dynamic`: the count can change; `identical`: every endpoint
/// has the same capabilities (so one CAPABILITY_GET describes them all).
struct EndpointReport
{
    std::uint8_t endpointCount = 0;
    bool dynamic               = false;
    bool identical             = false;
};

/// Decoded MULTI_CHANNEL_CAPABILITY_REPORT — one endpoint's device class
/// and the Command Classes it supports.
struct CapabilityReport
{
    std::uint8_t endpoint = 0;
    std::uint8_t generic  = 0;
    std::uint8_t specific = 0;
    std::vector<std::uint8_t> commandClasses;
};

/// Encode a MULTI_CHANNEL_END_POINT_GET (no payload).
[[nodiscard]] auto encodeEndpointGet() -> std::vector<std::uint8_t>;

/// Decode a MULTI_CHANNEL_END_POINT_REPORT. nullopt if not one.
[[nodiscard]] auto decodeEndpointReport(std::span<const std::uint8_t> payload) -> std::optional<EndpointReport>;

/// Encode a MULTI_CHANNEL_CAPABILITY_GET for `endpoint` (low 7 bits).
[[nodiscard]] auto encodeCapabilityGet(std::uint8_t endpoint) -> std::vector<std::uint8_t>;

/// Decode a MULTI_CHANNEL_CAPABILITY_REPORT. nullopt if not one.
[[nodiscard]] auto decodeCapabilityReport(std::span<const std::uint8_t> payload) -> std::optional<CapabilityReport>;
}  // namespace MultiChannel

#endif  // ZWAVED_MULTI_CHANNEL_HPP
