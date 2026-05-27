#ifndef ZWAVED_NODE_VERSION_HPP
#define ZWAVED_NODE_VERSION_HPP

// IWYU pragma: begin_exports
#include "NodeVersion.gen.hpp"
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>
#include <span>

/// Z-Wave Version Command Class (0x86) — reports a *node's* firmware
/// versions, distinct from the daemon's own version surface
/// (`Version::SEMVER`, exposed via the `GetVersion` D-Bus method).
/// The module is intentionally named `NodeVersion` to keep that
/// distinction loud at every call site.
///
/// The Report carries the node's Z-Wave library type (a HostApi
/// LIBRARY_TYPE_* value), Z-Wave protocol version (major.sub), and
/// application version (major.sub). v2 adds hardware version + a
/// per-firmware-target list; not decoded today.
///
/// Wire-byte oddity: Version's commands live in the 0x10s
/// (`VERSION_GET = 0x11`, `VERSION_REPORT = 0x12`) rather than the
/// usual 0x01-0x05. Constants come from NodeVersion.gen.hpp.
namespace NodeVersion
{
struct Report
{
    std::uint8_t libraryType           = 0;
    std::uint8_t protocolVersion       = 0;
    std::uint8_t protocolSubVersion    = 0;
    std::uint8_t applicationVersion    = 0;
    std::uint8_t applicationSubVersion = 0;
};

/// Decode a Version Report payload (the bytes inside an
/// APPLICATION_COMMAND_HANDLER frame, starting with COMMAND_CLASS).
/// Returns std::nullopt if the bytes are not a Version Report.
[[nodiscard]] auto decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>;
}  // namespace NodeVersion

#endif  // ZWAVED_NODE_VERSION_HPP
