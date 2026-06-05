#ifndef ZWAVED_SCENE_STORE_HPP
#define ZWAVED_SCENE_STORE_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// Durable store for daemon-side scenes (#119, #120). A *scene* is an
/// ordered list of actions — each a `SendData` payload addressed to a
/// target node. A *trigger* binds a physical press
/// `(sourceNodeId, sceneNumber, keyAttribute)` to a scene id, so the
/// SceneOrchestrator (#121) can look up "which scene does this button run".
///
/// Persistent (SQLite, shares `nodes.db` with the other stores) and scoped
/// per network by home ID, mirroring `PendingQueue` / `PolicyRegister`:
/// production code uses `SceneStore::instance()` (configured via the
/// retained `StorageConfig` bus event); unit tests construct
/// `SceneStore::Store` directly against tmp paths, and two instances on the
/// same file model "daemon stopped, daemon started again".
///
/// This module is the persistence layer only — the orchestrator (#121) and
/// the D-Bus surface (#122) build on top.
namespace SceneStore
{
/// One step of a scene: send `ccPayload` (a raw Command Class frame, the
/// same bytes a `SendData` carries) to `targetNodeId`.
struct Action
{
    std::uint8_t targetNodeId = 0;
    std::vector<std::uint8_t> ccPayload;
};

/// A press-to-scene binding.
struct Trigger
{
    std::uint8_t sourceNodeId = 0;
    std::uint8_t sceneNumber  = 0;
    std::uint8_t keyAttribute = 0;
    std::string sceneId;
};

/// One SQLite connection to one file; owns the `scenes` + `scene_triggers`
/// tables. Move-only (the sqlite3 handle is not copy-safe).
class Store
{
  public:
    /// Open / create the SQLite file at `dbPath`; creates the scene tables
    /// if absent, tolerant of the other modules' tables in the same file.
    explicit Store(const std::filesystem::path& dbPath);
    ~Store();

    Store(const Store&)                        = delete;
    auto operator=(const Store&) -> Store&     = delete;
    Store(Store&&) noexcept                    = default;
    auto operator=(Store&&) noexcept -> Store& = default;

    /// Bind to a network's 4-byte home ID; all calls scope to it (rows for
    /// other homes stay in the table, out of view). Mirrors the other
    /// stores' `setHomeId`, rebound from ProtocolThread on introspection.
    auto setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void;

    // ---- Scenes ----
    /// Create or replace scene `sceneId` with `actions` (in order).
    auto setScene(const std::string& sceneId, const std::vector<Action>& actions) -> void;
    /// Return scene `sceneId`'s ordered actions, or nullopt if absent.
    [[nodiscard]] auto getScene(const std::string& sceneId) const -> std::optional<std::vector<Action>>;
    /// Delete scene `sceneId` (no-op if absent). Does not touch triggers
    /// that reference it — a dangling trigger simply resolves to a missing
    /// scene, which the orchestrator treats as a no-op.
    auto deleteScene(const std::string& sceneId) -> void;
    /// All scene ids for the current home, ascending.
    [[nodiscard]] auto listScenes() const -> std::vector<std::string>;

    // ---- Triggers ----
    /// Bind (or rebind) a press to a scene id.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire fields, named at call sites
    auto bindTrigger(std::uint8_t sourceNodeId,
                     std::uint8_t sceneNumber,
                     std::uint8_t keyAttribute,
                     const std::string& sceneId) -> void;
    /// Remove a press binding (no-op if absent).
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire fields, named at call sites
    auto unbindTrigger(std::uint8_t sourceNodeId, std::uint8_t sceneNumber, std::uint8_t keyAttribute) -> void;
    /// Resolve a press to its scene id, or nullopt if no binding.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire fields, named at call sites
    [[nodiscard]] auto resolveTrigger(std::uint8_t sourceNodeId,
                                      std::uint8_t sceneNumber,
                                      std::uint8_t keyAttribute) const -> std::optional<std::string>;
    /// All triggers for the current home, ordered by (source, scene, key).
    [[nodiscard]] auto listTriggers() const -> std::vector<Trigger>;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

/// Production singleton. First call subscribes to `MessageBus::StorageConfig`
/// (replay-on-subscribe) and opens `${state_dir}/nodes.db`.
[[nodiscard]] auto instance() -> Store&;
}  // namespace SceneStore

#endif  // ZWAVED_SCENE_STORE_HPP
