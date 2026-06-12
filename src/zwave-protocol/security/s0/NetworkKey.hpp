#ifndef ZWAVED_S0_NETWORK_KEY_HPP
#define ZWAVED_S0_NETWORK_KEY_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"
// IWYU pragma: end_exports

#include <filesystem>
#include <optional>
#include <string>

/// Security S0 (CC 0x98) network-key persistence — phase 2 of the S0 epic
/// (#26 / #163). A single 16-byte key shared by every secure node, generated
/// once and reused for the life of the network (losing it forces re-inclusion
/// of every secure node). The bus wiring that resolves the key path from
/// config and announces readiness lives in NetworkKeyService.cpp; the
/// load-or-generate core here is pure and unit-tested against tmp paths.
namespace S0::NetworkKey
{
/// Outcome of resolving the key from disk.
struct Loaded
{
    Crypto::Key key{};
    bool generated = false;  ///< true if freshly created on this call (vs read from disk)
};

/// Resolve the key file path: `s0KeyFile` verbatim when non-empty, otherwise
/// `<stateDir>/security/s0.key` (with `stateDir` itself falling back to
/// $ZWAVED_STATE_DIR then the built-in default).
[[nodiscard]] auto resolvePath(const std::string& s0KeyFile, const std::string& stateDir) -> std::filesystem::path;

/// Load the 16-byte key from `path`, or generate a cryptographically random
/// one and persist it (parent dirs created, file `0600`, opened `O_CREAT |
/// O_EXCL` to avoid a TOCTOU race) when the file is absent. Returns
/// std::nullopt on any I/O / wrong-size / RNG failure — the caller treats that
/// as "S0 unavailable" rather than aborting the daemon.
[[nodiscard]] auto loadOrGenerate(const std::filesystem::path& path) -> std::optional<Loaded>;

/// The in-process network key, once `NetworkKeyService` has loaded or generated
/// it at startup; std::nullopt while S0 is unavailable. Set once during the
/// priority-111 constructor (before any worker thread starts) and read from the
/// bus thread thereafter, so the plain static needs no further synchronisation.
[[nodiscard]] auto current() -> std::optional<Crypto::Key>;

/// Publish the loaded key as the process-wide current key (called by the
/// service after a successful loadOrGenerate).
auto setCurrent(const Crypto::Key& key) -> void;
}  // namespace S0::NetworkKey

#endif  // ZWAVED_S0_NETWORK_KEY_HPP
