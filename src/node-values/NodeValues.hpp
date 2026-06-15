#ifndef ZWAVED_NODE_VALUES_HPP
#define ZWAVED_NODE_VALUES_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// Per-node **value cache** (#213): the last-known dynamic values a node has
/// reported (on/off, level, battery %, sensor readings, config params, …), each
/// with the time it was recorded. Lets the node-info view (#45) show "last known
/// value + when" without a live read — essential for sleeping nodes that can't
/// be polled on demand.
///
/// Distinct from its neighbours: node-registry holds slow-changing *identity*
/// (and its contract is "static info only"), node-metadata holds human *labels*.
/// This store holds the high-churn *values*, so the identity table doesn't
/// become write-hot. A `value_id` string names the logical value, with a
/// discriminator for multi-instance CCs (e.g. `battery`, `binary_switch`,
/// `config:3`, `sensor:1`, `setpoint:1`).
///
/// SQLite, sharing `nodes.db` on its own connection, keyed by
/// `(home_id, node_id, value_id)`. Follows the NodeMetadata / PendingQueue
/// split: a testable `Store` class (two instances against one file model a
/// restart) + a `NodeValues::instance()` singleton wired via `StorageConfig`.
/// The clock is injectable so tests get deterministic timestamps.
namespace NodeValues
{
/// One cached value: its logical id, the rendered value, and the unix-seconds
/// time it was recorded.
struct Entry
{
    std::string valueId;
    std::string value;
    std::int64_t updatedAt = 0;
};

/// Returns the current time as unix seconds. Injectable for tests.
using Clock = std::function<std::int64_t()>;

/// The default clock: wall-clock unix seconds.
[[nodiscard]] auto systemClock() -> std::int64_t;

/// One instance owns one sqlite3 connection to one file. Move-only.
class Store
{
  public:
    explicit Store(const std::filesystem::path& dbPath, Clock clock = systemClock);
    ~Store();

    Store(const Store&)                        = delete;
    auto operator=(const Store&) -> Store&     = delete;
    Store(Store&&) noexcept                    = default;
    auto operator=(Store&&) noexcept -> Store& = default;

    /// Bind to a network's 4-byte home ID (same place ProtocolThread rebinds
    /// NodeRegistry / PendingQueue / NodeMetadata). All calls scope to it.
    auto setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void;

    /// Upsert `valueId = value` for `nodeId`, stamping it with the clock's now.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): valueId and value are clearly named at call sites
    auto record(std::uint8_t nodeId, const std::string& valueId, const std::string& value) -> void;

    [[nodiscard]] auto get(std::uint8_t nodeId, const std::string& valueId) const -> std::optional<Entry>;

    /// Every cached value for `nodeId`, ordered by valueId.
    [[nodiscard]] auto getAll(std::uint8_t nodeId) const -> std::vector<Entry>;

    /// Drop every cached value for `nodeId` (e.g. on exclusion).
    auto clearForNode(std::uint8_t nodeId) -> void;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

/// Production singleton — opens `${state_dir}/nodes.db` (state dir from the
/// retained `StorageConfig` event).
[[nodiscard]] auto instance() -> Store&;
}  // namespace NodeValues

#endif  // ZWAVED_NODE_VALUES_HPP
