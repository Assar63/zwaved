#ifndef ZWAVED_S2_TRANSPORT_HPP
#define ZWAVED_S2_TRANSPORT_HPP

// IWYU pragma: begin_exports
#include "KeyDerivation.hpp"  // S2::KeyDerivation::NetworkKeys
#include "SpanManager.hpp"    // S2::SpanManager
// IWYU pragma: end_exports

#include <cstdint>
#include <optional>

/// Security S2 (CC 0x9F) live-transport runtime (#199) — the process-wide SPAN
/// state shared between the inbound decap seam and the outbound encrypt path, so
/// a SPAN established in one direction is reused by the other (bootstrap, #187,
/// keeps its own short-lived temp-channel SpanManager — this one carries the
/// permanent per-class keys).
namespace S2::Transport
{
/// The process-wide per-peer transport SPAN manager.
[[nodiscard]] auto manager() -> SpanManager&;

/// Derive the {KeyCCM, PersonalizationString, KeyMPAN} a node's traffic uses
/// from its recorded security scheme (the numeric NodeRegistry::SecurityScheme:
/// 2 S2-Unauthenticated, 3 S2-Authenticated, 4 S2-Access-Control) and the loaded
/// network keys. std::nullopt if the scheme isn't an S2 class or the keys aren't
/// loaded.
[[nodiscard]] auto resolveClassKeys(std::uint8_t securityScheme) -> std::optional<KeyDerivation::NetworkKeys>;
}  // namespace S2::Transport

#endif  // ZWAVED_S2_TRANSPORT_HPP
