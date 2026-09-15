#include "PendingQueue.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../sqlite/Sqlite.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <sqlite3.h>

namespace
{
constexpr const char* DEFAULT_STATE_DIR = "/var/lib/zwaved";
constexpr const char* STATE_DIR_ENV     = "ZWAVED_STATE_DIR";
constexpr const char* DB_FILENAME       = "nodes.db";

constexpr const char* SCHEMA_SQL = R"(
CREATE TABLE IF NOT EXISTS pending_commands (
    sequence    INTEGER PRIMARY KEY AUTOINCREMENT,
    home_id     TEXT    NOT NULL,
    node_id     INTEGER NOT NULL,
    priority    INTEGER NOT NULL DEFAULT 100,
    payload     BLOB    NOT NULL,
    enqueued_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_pending_node
    ON pending_commands(home_id, node_id, priority, sequence);
)";

constexpr const char* INSERT_SQL = "INSERT INTO pending_commands (home_id, node_id, priority, payload, enqueued_at) "
                                   "VALUES (?, ?, ?, ?, ?)";

constexpr const char* SELECT_FOR_NODE_SQL = "SELECT sequence, priority, payload FROM pending_commands "
                                            "WHERE home_id = ? AND node_id = ? "
                                            "ORDER BY priority ASC, sequence ASC";

constexpr const char* DELETE_FOR_NODE_SQL = "DELETE FROM pending_commands WHERE home_id = ? AND node_id = ?";

// Bind positions for INSERT_SQL (1-based per sqlite3_bind_*).
constexpr int BIND_INSERT_HOME        = 1;
constexpr int BIND_INSERT_NODE_ID     = 2;
constexpr int BIND_INSERT_PRIORITY    = 3;
constexpr int BIND_INSERT_PAYLOAD     = 4;
constexpr int BIND_INSERT_ENQUEUED_AT = 5;

// SELECT column indices for SELECT_FOR_NODE_SQL (0-based).
constexpr int SELECT_COL_SEQUENCE = 0;
constexpr int SELECT_COL_PRIORITY = 1;
constexpr int SELECT_COL_PAYLOAD  = 2;

auto formatHomeId(const std::vector<std::uint8_t>& bytes) -> std::string
{
    std::ostringstream stream;
    stream << std::hex << std::uppercase << std::setfill('0');
    for (const auto byte : bytes)
    {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

auto epochSeconds() -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): pimpl, public members read like a struct
struct PendingQueue::Queue::State
{
    mutable std::mutex mutex;
    Sqlite::Db db;
    std::optional<std::string> currentHomeId;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

PendingQueue::Queue::Queue(const std::filesystem::path& dbPath)
    : state_(std::make_unique<State>())
{
    std::error_code errorCode;
    std::filesystem::create_directories(dbPath.parent_path(), errorCode);
    if (errorCode)
    {
        Logger::error("[PendingQueue] cannot create state dir " + dbPath.parent_path().string() + ": " +
                      errorCode.message());
        return;
    }
    state_->db = Sqlite::Db(dbPath, "PendingQueue");
    if (!state_->db.valid())
    {
        return;
    }
    if (!state_->db.exec(SCHEMA_SQL, "CREATE TABLE"))
    {
        state_->db.close();
        return;
    }
    Logger::info("[PendingQueue] db ready at " + dbPath.string());
}

PendingQueue::Queue::~Queue() = default;

auto PendingQueue::Queue::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    const std::scoped_lock lock(state_->mutex);
    state_->currentHomeId = formatHomeId(homeIdBytes);
}

auto PendingQueue::Queue::enqueue(std::uint8_t nodeId, std::vector<std::uint8_t> payload, std::uint8_t priority) -> void
{
    std::optional<MessageBus::PendingCommandEnqueued> event;
    {
        const std::scoped_lock lock(state_->mutex);
        if (!state_->db.valid() || !state_->currentHomeId.has_value())
        {
            Logger::warn("[PendingQueue] enqueue dropped — no DB / no home (node " + std::to_string(nodeId) + ")");
            return;
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track through the short-circuit
        const std::string& home = *state_->currentHomeId;
        auto stmt               = state_->db.prepare(INSERT_SQL, "INSERT");
        if (!stmt.valid())
        {
            return;
        }
        stmt.bindText(BIND_INSERT_HOME, home)
            .bindInt(BIND_INSERT_NODE_ID, nodeId)
            .bindInt(BIND_INSERT_PRIORITY, priority)
            .bindBlob(BIND_INSERT_PAYLOAD, payload.data(), static_cast<int>(payload.size()))
            .bindInt64(BIND_INSERT_ENQUEUED_AT, epochSeconds())
            .execDone();
        const auto sequence = static_cast<std::uint32_t>(sqlite3_last_insert_rowid(state_->db.raw()));
        event               = MessageBus::PendingCommandEnqueued{
                          .nodeId   = nodeId,
                          .sequence = sequence,
                          .priority = priority,
        };
    }
    MessageBus::publish(*event);
}

auto PendingQueue::Queue::drain(std::uint8_t nodeId) -> std::vector<std::vector<std::uint8_t>>
{
    std::vector<std::vector<std::uint8_t>> out;
    {
        const std::scoped_lock lock(state_->mutex);
        if (!state_->db.valid() || !state_->currentHomeId.has_value())
        {
            // Publish drained=0 so callers always see *some* event
            // back even when the daemon hasn't bound a home yet.
            MessageBus::publish(MessageBus::PendingCommandsDrained{.nodeId = nodeId, .count = 0});
            return out;
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track through the short-circuit
        const std::string& home = *state_->currentHomeId;
        auto select             = state_->db.prepare(SELECT_FOR_NODE_SQL, "SELECT");
        if (!select.valid())
        {
            return out;
        }
        select.bindText(1, home).bindInt(2, nodeId);
        while (select.step() == SQLITE_ROW)
        {
            const auto* blob = static_cast<const std::uint8_t*>(sqlite3_column_blob(select.raw(), SELECT_COL_PAYLOAD));
            const int blobSize = sqlite3_column_bytes(select.raw(), SELECT_COL_PAYLOAD);
            if (blob != nullptr && blobSize > 0)
            {
                out.emplace_back(blob, blob + blobSize);
            }
        }
        // Single DELETE for the whole node — cheaper than per-row.
        auto del = state_->db.prepare(DELETE_FOR_NODE_SQL, "DELETE");
        if (del.valid())
        {
            del.bindText(1, home).bindInt(2, nodeId).execDone();
        }
    }
    MessageBus::publish(MessageBus::PendingCommandsDrained{
        .nodeId = nodeId,
        .count  = static_cast<std::uint32_t>(out.size()),
    });
    return out;
}

auto PendingQueue::Queue::peek(std::uint8_t nodeId) const -> std::vector<Entry>
{
    std::vector<Entry> out;
    const std::scoped_lock lock(state_->mutex);
    if (!state_->db.valid() || !state_->currentHomeId.has_value())
    {
        return out;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track through the short-circuit
    const std::string& home = *state_->currentHomeId;
    auto stmt               = state_->db.prepare(SELECT_FOR_NODE_SQL, "SELECT (peek)");
    if (!stmt.valid())
    {
        return out;
    }
    stmt.bindText(1, home).bindInt(2, nodeId);
    while (stmt.step() == SQLITE_ROW)
    {
        Entry entry;
        entry.sequence     = static_cast<std::uint32_t>(sqlite3_column_int64(stmt.raw(), SELECT_COL_SEQUENCE));
        entry.priority     = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), SELECT_COL_PRIORITY));
        const auto* blob   = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt.raw(), SELECT_COL_PAYLOAD));
        const int blobSize = sqlite3_column_bytes(stmt.raw(), SELECT_COL_PAYLOAD);
        if (blob != nullptr && blobSize > 0)
        {
            entry.payload.assign(blob, blob + blobSize);
        }
        out.push_back(std::move(entry));
    }
    return out;
}

auto PendingQueue::Queue::clearForNode(std::uint8_t nodeId) -> void
{
    const std::scoped_lock lock(state_->mutex);
    if (!state_->db.valid() || !state_->currentHomeId.has_value())
    {
        return;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track through the short-circuit
    const std::string& home = *state_->currentHomeId;
    auto stmt               = state_->db.prepare(DELETE_FOR_NODE_SQL, "DELETE (clearForNode)");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(1, home).bindInt(2, nodeId).execDone();
}

// ---- Production singleton ---------------------------------------

namespace
{
struct SingletonState
{
    std::string configuredStateDir;
    MessageBus::SubscriptionGuard storageSub;
    std::unique_ptr<PendingQueue::Queue> queue;
    std::once_flag initFlag;
};

auto singletonState() -> SingletonState&
{
    static SingletonState instance;
    return instance;
}

auto resolveDbPath() -> std::filesystem::path
{
    if (!singletonState().configuredStateDir.empty())
    {
        return std::filesystem::path(singletonState().configuredStateDir) / DB_FILENAME;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe): runs once during call_once-protected init
    const char* env       = std::getenv(STATE_DIR_ENV);
    const std::string dir = (env != nullptr && *env != '\0') ? env : DEFAULT_STATE_DIR;
    return std::filesystem::path(dir) / DB_FILENAME;
}
}  // namespace

auto PendingQueue::instance() -> Queue&
{
    std::call_once(singletonState().initFlag,
                   []
                   {
                       singletonState().storageSub =
                           MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::StorageConfig>(
                               [](const MessageBus::StorageConfig& cfg) -> void
                               { singletonState().configuredStateDir = cfg.stateDir; }));
                       singletonState().queue = std::make_unique<Queue>(resolveDbPath());
                   });
    return *singletonState().queue;
}
