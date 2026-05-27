#ifndef ZWAVED_PENDING_QUEUE_HPP
#define ZWAVED_PENDING_QUEUE_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

/// Durable per-node queue of outbound payloads. When the daemon
/// decides node N needs a Set / Get but N is asleep, the
/// `SendData` payload bytes go here; on the next
/// `WAKE_UP_NOTIFICATION` from N, WakeUpOrchestrator (#68) drains
/// the queue and ferries each payload through ProtocolThread. The
/// queue is persistent (SQLite, shares `nodes.db` with
/// NodeRegistry) so a daemon restart doesn't lose pending payloads
/// for sleeping nodes.
///
/// Production code uses `PendingQueue::instance()` — a singleton
/// configured via the `StorageConfig` retained bus event, same
/// pattern NodeRegistry follows. Unit tests construct
/// `PendingQueue::Queue` instances directly against tmp paths;
/// two instances pointing at the same file model
/// "daemon stopped, daemon started again" semantics cleanly.
namespace PendingQueue
{
/// Lower number = sooner-drained. The defaults are sparse on
/// purpose so finer-grained priorities can slot between them
/// without renumbering the existing ones.
inline constexpr std::uint8_t PRIORITY_HIGH   = 50;
inline constexpr std::uint8_t PRIORITY_NORMAL = 100;
inline constexpr std::uint8_t PRIORITY_LOW    = 200;

/// One entry in a `peek()` snapshot. Returned in the same
/// (priority asc, sequence asc) order `drain()` would pop them.
struct Entry
{
    std::uint32_t sequence = 0;
    std::uint8_t priority  = PRIORITY_NORMAL;
    std::vector<std::uint8_t> payload;
};

/// Persistent per-node command queue. One instance owns one
/// sqlite3 connection to one file. Move-only — the underlying
/// sqlite3 handle is not copy-safe.
class Queue
{
  public:
    /// Open / create the SQLite file at `dbPath`. Creates the
    /// pending_commands table (and its lookup index) if it
    /// doesn't already exist; tolerant of co-existing tables
    /// owned by other modules (NodeRegistry's `nodes` table).
    explicit Queue(const std::filesystem::path& dbPath);
    ~Queue();

    Queue(const Queue&)                        = delete;
    auto operator=(const Queue&) -> Queue&     = delete;
    Queue(Queue&&) noexcept                    = default;
    auto operator=(Queue&&) noexcept -> Queue& = default;

    /// Bind the queue to a Z-Wave network's 4-byte home ID
    /// (typically called from ProtocolThread right after
    /// `NodeRegistry::setHomeId`). All enqueue / drain /
    /// clearForNode calls scope to this home; rows for other
    /// home IDs stay in the table, just out of view.
    auto setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void;

    /// Append a payload for `nodeId` at `priority`. Publishes a
    /// `PendingCommandEnqueued` bus event on success. No-op if
    /// no home ID has been bound yet (publishes a warning to
    /// the Logger).
    auto enqueue(std::uint8_t nodeId,
                 std::vector<std::uint8_t> payload,
                 std::uint8_t priority = PRIORITY_NORMAL) -> void;

    /// Pop every entry for `nodeId` in (priority asc, sequence
    /// asc) order. Returns an empty vector if no entries (and
    /// still publishes `PendingCommandsDrained{count=0}` so
    /// WakeUpOrchestrator's "empty wake-up" path is observable).
    [[nodiscard]] auto drain(std::uint8_t nodeId) -> std::vector<std::vector<std::uint8_t>>;

    /// Read-only snapshot of `nodeId`'s pending entries, in the
    /// order `drain()` would pop them. Used by diagnostics
    /// (e.g. zwave-terminal) and by tests.
    [[nodiscard]] auto peek(std::uint8_t nodeId) const -> std::vector<Entry>;

    /// Delete all pending entries for `nodeId` regardless of
    /// priority. Called when a node is removed from the network
    /// — there's no point in queueing for a node that's gone.
    auto clearForNode(std::uint8_t nodeId) -> void;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

/// Production singleton. First call subscribes to
/// `MessageBus::StorageConfig` (replay-on-subscribe sets the
/// configured state dir before the connection is opened) and
/// opens `${state_dir}/nodes.db`. Throws on SQLite open failure
/// — the daemon can't run usefully without a queue.
[[nodiscard]] auto instance() -> Queue&;
}  // namespace PendingQueue

#endif  // ZWAVED_PENDING_QUEUE_HPP
