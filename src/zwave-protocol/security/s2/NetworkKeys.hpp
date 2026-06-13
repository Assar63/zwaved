#ifndef ZWAVED_S2_NETWORK_KEYS_HPP
#define ZWAVED_S2_NETWORK_KEYS_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"  // S2::Crypto::Key
// IWYU pragma: end_exports

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

/// Security S2 (CC 0x9F) multi-class network-key persistence — phase 2 of the
/// S2 epic (#27 / #180). S2 holds one 16-byte key per security class; the
/// inclusion handshake grants a node a subset of classes, and the
/// decapsulator picks the matching key from a frame's class byte.
namespace S2::NetworkKeys
{
/// The S2 key classes, in their canonical order (also the KeySet index).
enum class Class : std::uint8_t
{
    Unauthenticated = 0,  ///< included without DSK confirmation
    Authenticated   = 1,  ///< DSK confirmed at inclusion
    AccessControl   = 2,  ///< locks; strongest pairing
    S0Compat        = 3,  ///< S0 traffic bridged into an S2 network
};
constexpr std::size_t CLASS_COUNT = 4;

using KeySet = std::array<Crypto::Key, CLASS_COUNT>;

struct Loaded
{
    KeySet keys{};
    std::array<bool, CLASS_COUNT> generated{};  ///< per-class: freshly created this run
};

/// The key directory: `s2KeyDir` verbatim when non-empty, else
/// `<stateDir>/security/s2` (with `stateDir` falling back to
/// $ZWAVED_STATE_DIR then the built-in default).
[[nodiscard]] auto resolveDir(const std::string& s2KeyDir, const std::string& stateDir) -> std::filesystem::path;

/// File name for a class's key within the directory (e.g. `auth.key`).
[[nodiscard]] auto fileName(Class cls) -> std::string;

/// Load every class key from `dir`, generating + persisting (mode `0600`,
/// `O_CREAT | O_EXCL`) any that are absent. Returns std::nullopt if any class
/// can't be loaded or generated — S2 needs all four, so it's all-or-nothing.
[[nodiscard]] auto loadOrGenerateAll(const std::filesystem::path& dir) -> std::optional<Loaded>;

/// Index a KeySet by class.
[[nodiscard]] auto keyFor(const KeySet& keys, Class cls) -> const Crypto::Key&;

/// The in-process key set, once `NetworkKeysService` has loaded/generated it at
/// startup; std::nullopt while S2 is unavailable. Set once during the
/// priority-111 constructor (before any worker thread), read on the bus thread.
[[nodiscard]] auto current() -> std::optional<KeySet>;

/// Publish the loaded key set as the process-wide current set.
auto setCurrent(const KeySet& keys) -> void;
}  // namespace S2::NetworkKeys

#endif  // ZWAVED_S2_NETWORK_KEYS_HPP
