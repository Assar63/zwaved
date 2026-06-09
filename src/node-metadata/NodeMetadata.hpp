#ifndef ZWAVED_NODE_METADATA_HPP
#define ZWAVED_NODE_METADATA_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// Human-authored, purely descriptive per-node metadata — free-form
/// key/value strings like `name=Kitchen light`, `room=Kitchen`,
/// `purpose=main light`. The daemon never acts on this; it exists so
/// operators / UIs can label a network in human terms instead of bare
/// node IDs. Distinct from PolicyRegister (#66), which is *behavioural*
/// config the orchestrators apply.
///
/// Persistent — a `node_metadata` table in the shared `nodes.db`, on this
/// module's own SQLite connection, keyed by `(home_id, node_id, key)` so
/// it survives restarts and stays isolated per network. Production code
/// uses `NodeMetadata::instance()`; unit tests build `NodeMetadata::Store`
/// directly against a tmp path, same split as PolicyRegister / PendingQueue.
namespace NodeMetadata
{
/// One key/value pair for a node.
struct Entry
{
    std::string key;
    std::string value;
};

class Store
{
  public:
    explicit Store(const std::filesystem::path& dbPath);
    ~Store();

    Store(const Store&)                        = delete;
    auto operator=(const Store&) -> Store&     = delete;
    Store(Store&&) noexcept                    = default;
    auto operator=(Store&&) noexcept -> Store& = default;

    /// Bind to a network's 4-byte home ID (same place ProtocolThread
    /// rebinds NodeRegistry / PendingQueue / PolicyRegister).
    auto setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void;

    /// Upsert `key=value` for `nodeId`. An **empty** `value` deletes the
    /// key (so a UI can clear a field with the same call it sets one).
    /// Publishes `NodeMetadataChanged{nodeId}`.
    auto set(std::uint8_t nodeId, const std::string& key, const std::string& value) -> void;

    /// Delete a single key for `nodeId`. Publishes `NodeMetadataChanged`.
    auto remove(std::uint8_t nodeId, const std::string& key) -> void;

    [[nodiscard]] auto get(std::uint8_t nodeId, const std::string& key) const -> std::optional<std::string>;

    /// All key/value pairs for `nodeId`, ordered by key.
    [[nodiscard]] auto getAll(std::uint8_t nodeId) const -> std::vector<Entry>;

    /// Reverse lookup: every node in the current network carrying the exact
    /// tag `key=value`, ascending node id. The membership resolver the
    /// logical thermostat (#131) builds on — "which nodes are tagged
    /// `room=living-room`". Empty if none / no DB / no home bound.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): key and value are clearly named at call sites
    [[nodiscard]] auto nodesWith(const std::string& key, const std::string& value) const -> std::vector<std::uint8_t>;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

/// Production singleton — opens `${state_dir}/nodes.db` (state dir from
/// the retained `StorageConfig` event).
[[nodiscard]] auto instance() -> Store&;
}  // namespace NodeMetadata

#endif  // ZWAVED_NODE_METADATA_HPP
