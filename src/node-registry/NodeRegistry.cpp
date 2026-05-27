#include "NodeRegistry.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <map>
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
constexpr int CURRENT_SCHEMA_VERSION    = 1;

constexpr const char* SCHEMA_SQL = R"(
CREATE TABLE IF NOT EXISTS nodes (
    home_id         TEXT    NOT NULL,
    node_id         INTEGER NOT NULL,
    basic_type      INTEGER NOT NULL,
    generic_type    INTEGER NOT NULL,
    specific_type   INTEGER NOT NULL,
    command_classes BLOB,
    PRIMARY KEY (home_id, node_id)
)
)";

constexpr const char* UPSERT_SQL =
    "INSERT INTO nodes (home_id, node_id, basic_type, generic_type, specific_type, command_classes) "
    "VALUES (?, ?, ?, ?, ?, ?) "
    "ON CONFLICT(home_id, node_id) DO UPDATE SET "
    "basic_type      = excluded.basic_type, "
    "generic_type    = excluded.generic_type, "
    "specific_type   = excluded.specific_type, "
    "command_classes = excluded.command_classes";

constexpr const char* DELETE_SQL = "DELETE FROM nodes WHERE home_id = ? AND node_id = ?";

constexpr const char* SELECT_FOR_HOME_SQL =
    "SELECT node_id, basic_type, generic_type, specific_type, command_classes FROM nodes WHERE home_id = ?";

// Bind positions for UPSERT (1-based per sqlite3_bind_*).
constexpr int BIND_HOME     = 1;
constexpr int BIND_NODE_ID  = 2;
constexpr int BIND_BASIC    = 3;
constexpr int BIND_GENERIC  = 4;
constexpr int BIND_SPECIFIC = 5;
constexpr int BIND_CCS      = 6;

// SELECT column indices (0-based).
constexpr int COL_NODE_ID  = 0;
constexpr int COL_BASIC    = 1;
constexpr int COL_GENERIC  = 2;
constexpr int COL_SPECIFIC = 3;
constexpr int COL_CCS      = 4;

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    std::mutex mutex;
    std::map<std::uint8_t, NodeRegistry::NodeInfo> nodes;
    std::optional<std::string> currentHomeId;  // hex form, e.g. "E2A1B07C"
    sqlite3* db = nullptr;
    std::once_flag initFlag;

    // Cached state directory from `MessageBus::StorageConfig`.
    // Subscribed to during initIfNeeded() so the bus's
    // replay-on-subscribe delivers the value synchronously before we
    // open the database. Empty means "fall back to env / built-in".
    std::string configuredStateDir;
    MessageBus::SubscriptionId storageSub{0};

    ~State()
    {
        if (storageSub != 0)
        {
            MessageBus::unsubscribe(storageSub);
            storageSub = 0;
        }
        if (db != nullptr)
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    State()                                    = default;
    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> State&
{
    static State instance;
    return instance;
}

/// RAII wrapper around `sqlite3_stmt*`. Prepares on construction
/// (logging on failure), finalizes on destruction. Bind methods
/// chain. `valid()` reports whether prepare succeeded — callers
/// must check before stepping.
class Stmt
{
  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SQL and label are clearly distinct at call sites
    Stmt(sqlite3* database, const char* sql, const char* label)
        : database_(database),
          label_(label)
    {
        if (sqlite3_prepare_v2(database, sql, -1, &stmt_, nullptr) != SQLITE_OK)
        {
            Logger::error(std::string("[NodeRegistry] prepare ") + label + " failed: " + sqlite3_errmsg(database));
            stmt_ = nullptr;
        }
    }

    ~Stmt()
    {
        if (stmt_ != nullptr)
        {
            sqlite3_finalize(stmt_);
        }
    }

    Stmt(const Stmt&)                        = delete;
    auto operator=(const Stmt&) -> Stmt&     = delete;
    Stmt(Stmt&&) noexcept                    = delete;
    auto operator=(Stmt&&) noexcept -> Stmt& = delete;

    [[nodiscard]] auto valid() const -> bool
    {
        return stmt_ != nullptr;
    }

    auto bindText(int pos, const std::string& value) -> Stmt&
    {
        sqlite3_bind_text(stmt_, pos, value.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }

    auto bindInt(int pos, int value) -> Stmt&
    {
        sqlite3_bind_int(stmt_, pos, value);
        return *this;
    }

    auto bindBlob(int pos, const void* data, int size) -> Stmt&
    {
        sqlite3_bind_blob(stmt_, pos, data, size, SQLITE_TRANSIENT);
        return *this;
    }

    auto step() -> int
    {
        return sqlite3_step(stmt_);
    }

    /// Execute a statement expected to terminate with SQLITE_DONE.
    /// Logs the SQLite error message if it doesn't.
    auto execDone() -> void
    {
        if (sqlite3_step(stmt_) != SQLITE_DONE)
        {
            Logger::error(std::string("[NodeRegistry] ") + label_ + " failed: " + sqlite3_errmsg(database_));
        }
    }

    [[nodiscard]] auto raw() const -> sqlite3_stmt*
    {
        return stmt_;
    }

  private:
    sqlite3_stmt* stmt_ = nullptr;
    sqlite3* database_  = nullptr;
    const char* label_  = nullptr;
};

auto resolveDbPath() -> std::filesystem::path
{
    // Resolution order:
    //   1. config.toml [storage] state_dir, if non-empty (fed in via
    //      MessageBus::StorageConfig — populated during initIfNeeded
    //      via replay-on-subscribe).
    //   2. ZWAVED_STATE_DIR env var, if set.
    //   3. Built-in default (/var/lib/zwaved).
    if (!state().configuredStateDir.empty())
    {
        return std::filesystem::path(state().configuredStateDir) / DB_FILENAME;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe): runs once during call_once-protected init
    const char* env       = std::getenv(STATE_DIR_ENV);
    const std::string dir = (env != nullptr && *env != '\0') ? env : DEFAULT_STATE_DIR;
    return std::filesystem::path(dir) / DB_FILENAME;
}

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

auto readSchemaVersion(sqlite3* database) -> int
{
    Stmt stmt(database, "PRAGMA user_version", "PRAGMA user_version");
    if (!stmt.valid())
    {
        return 0;
    }
    if (stmt.step() == SQLITE_ROW)
    {
        return sqlite3_column_int(stmt.raw(), 0);
    }
    return 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SQL and label are clearly distinct at call sites
auto execOrLog(sqlite3* database, const char* sql, const char* what) -> bool
{
    char* err = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &err) != SQLITE_OK)
    {
        Logger::error(std::string("[NodeRegistry] ") + what + " failed: " + (err != nullptr ? err : "?"));
        sqlite3_free(err);
        return false;
    }
    return true;
}

auto migrateSchema(sqlite3* database) -> bool
{
    if (readSchemaVersion(database) >= CURRENT_SCHEMA_VERSION)
    {
        return true;
    }
    // Pre-v1 databases keyed nodes by node_id only — they can't be reused
    // safely under a different dongle. Drop and recreate; GET_INIT_DATA on
    // the next connect re-seeds from the dongle's bitmap.
    if (!execOrLog(database, "DROP TABLE IF EXISTS nodes", "DROP TABLE"))
    {
        return false;
    }
    if (!execOrLog(database, SCHEMA_SQL, "CREATE TABLE"))
    {
        return false;
    }
    if (!execOrLog(database, "PRAGMA user_version = 1", "PRAGMA user_version"))
    {
        return false;
    }
    Logger::info("[NodeRegistry] migrated schema to version " + std::to_string(CURRENT_SCHEMA_VERSION));
    return true;
}

auto loadNodesForHome(sqlite3* database, const std::string& homeId) -> std::map<std::uint8_t, NodeRegistry::NodeInfo>
{
    std::map<std::uint8_t, NodeRegistry::NodeInfo> result;
    Stmt stmt(database, SELECT_FOR_HOME_SQL, "SELECT");
    if (!stmt.valid())
    {
        return result;
    }
    stmt.bindText(1, homeId);
    while (stmt.step() == SQLITE_ROW)
    {
        NodeRegistry::NodeInfo info;
        info.nodeId       = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), COL_NODE_ID));
        info.basicType    = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), COL_BASIC));
        info.genericType  = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), COL_GENERIC));
        info.specificType = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), COL_SPECIFIC));

        const auto* blob   = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt.raw(), COL_CCS));
        const int blobSize = sqlite3_column_bytes(stmt.raw(), COL_CCS);
        if (blob != nullptr && blobSize > 0)
        {
            info.commandClasses.assign(blob, blob + blobSize);
        }
        result.emplace(info.nodeId, info);
    }
    return result;
}

auto initIfNeeded() -> void
{
    std::call_once(
        state().initFlag,
        []
        {
            // Subscribe synchronously so the bus's retained
            // StorageConfig (published by Config at priority 102,
            // long before the registry is first touched at runtime)
            // populates `configuredStateDir` *before* we open the
            // database below.
            state().storageSub = MessageBus::subscribe<MessageBus::StorageConfig>(
                [](const MessageBus::StorageConfig& cfg) -> void { state().configuredStateDir = cfg.stateDir; });

            const auto path = resolveDbPath();
            std::error_code errorCode;
            std::filesystem::create_directories(path.parent_path(), errorCode);
            if (errorCode)
            {
                Logger::error("[NodeRegistry] cannot create state dir " + path.parent_path().string() + ": " +
                              errorCode.message() + " — falling back to in-memory only");
                return;
            }
            if (sqlite3_open(path.c_str(), &state().db) != SQLITE_OK)
            {
                Logger::error("[NodeRegistry] cannot open " + path.string() + ": " + sqlite3_errmsg(state().db) +
                              " — falling back to in-memory only");
                sqlite3_close(state().db);
                state().db = nullptr;
                return;
            }
            if (!migrateSchema(state().db))
            {
                sqlite3_close(state().db);
                state().db = nullptr;
                return;
            }
            Logger::info("[NodeRegistry] db ready at " + path.string());
        });
}

auto persistAdd(const std::string& homeId, const NodeRegistry::NodeInfo& info) -> void
{
    if (state().db == nullptr)
    {
        return;
    }
    Stmt stmt(state().db, UPSERT_SQL, "UPSERT");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(BIND_HOME, homeId)
        .bindInt(BIND_NODE_ID, info.nodeId)
        .bindInt(BIND_BASIC, info.basicType)
        .bindInt(BIND_GENERIC, info.genericType)
        .bindInt(BIND_SPECIFIC, info.specificType)
        .bindBlob(BIND_CCS, info.commandClasses.data(), static_cast<int>(info.commandClasses.size()))
        .execDone();
}

auto persistRemove(const std::string& homeId, std::uint8_t nodeId) -> void
{
    if (state().db == nullptr)
    {
        return;
    }
    Stmt stmt(state().db, DELETE_SQL, "DELETE");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(1, homeId).bindInt(2, nodeId).execDone();
}

/// Build a NodeListChanged from the in-memory map. Caller holds the
/// state mutex.
auto snapshotEvent() -> MessageBus::NodeListChanged
{
    MessageBus::NodeListChanged event;
    event.nodes.reserve(state().nodes.size());
    for (const auto& [_id, info] : state().nodes)
    {
        event.nodes.push_back({.nodeId         = info.nodeId,
                               .basicType      = info.basicType,
                               .genericType    = info.genericType,
                               .specificType   = info.specificType,
                               .commandClasses = info.commandClasses});
    }
    return event;
}

/// Common scaffold for add/remove/seed/updateDeviceClass: init, lock,
/// require a bound home, run the mutator, snapshot, and publish the
/// resulting NodeListChanged outside the lock.
///
/// The mutator returns `true` to request publication (state actually
/// changed) or `false` to skip — used by `seed`, which is a no-op when
/// the entry already exists, and by `updateDeviceClass`, which is a
/// no-op when the entry is missing.
template <typename Mutator> auto withBoundHome(Mutator mutator) -> void
{
    initIfNeeded();
    std::optional<MessageBus::NodeListChanged> event;
    {
        std::scoped_lock const lock(state().mutex);
        const auto& home = state().currentHomeId;
        if (!home.has_value())
        {
            return;
        }
        if (!mutator(*home))
        {
            return;
        }
        event = snapshotEvent();
    }
    MessageBus::publish(*event);
}
}  // namespace

auto NodeRegistry::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    initIfNeeded();
    auto homeIdStr = formatHomeId(homeIdBytes);
    std::optional<MessageBus::NodeListChanged> event;
    {
        std::scoped_lock const lock(state().mutex);
        if (const auto current = state().currentHomeId; current.has_value() && *current == homeIdStr)
        {
            return;
        }
        state().currentHomeId = homeIdStr;
        state().nodes.clear();
        if (state().db != nullptr)
        {
            state().nodes = loadNodesForHome(state().db, homeIdStr);
        }
        Logger::info("[NodeRegistry] bound to home " + homeIdStr + " (" + std::to_string(state().nodes.size()) +
                     " node(s) loaded)");
        event = snapshotEvent();
    }
    MessageBus::publish(*event);
}

auto NodeRegistry::add(const NodeInfo& info) -> void
{
    withBoundHome(
        [&](const std::string& home) -> bool
        {
            state().nodes[info.nodeId] = info;
            persistAdd(home, info);
            return true;
        });
}

auto NodeRegistry::remove(std::uint8_t nodeId) -> void
{
    withBoundHome(
        [&](const std::string& home) -> bool
        {
            state().nodes.erase(nodeId);
            persistRemove(home, nodeId);
            return true;
        });
}

auto NodeRegistry::seed(std::uint8_t nodeId) -> void
{
    withBoundHome(
        [&](const std::string& home) -> bool
        {
            if (state().nodes.find(nodeId) != state().nodes.end())
            {
                return false;
            }
            NodeInfo info;
            info.nodeId           = nodeId;
            state().nodes[nodeId] = info;
            persistAdd(home, info);
            return true;
        });
}

auto NodeRegistry::updateCommandClasses(std::uint8_t nodeId, std::vector<std::uint8_t> commandClasses) -> void
{
    withBoundHome(
        [&](const std::string& home) -> bool
        {
            const auto iter = state().nodes.find(nodeId);
            if (iter == state().nodes.end())
            {
                return false;
            }
            // Same UPSERT trick as updateDeviceClass — persistAdd
            // overwrites the row, preserving the device-class triple.
            iter->second.commandClasses = std::move(commandClasses);
            persistAdd(home, iter->second);
            return true;
        });
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire shape fixed by FUNC_ID_GET_NODE_PROTOCOL_INFO
auto NodeRegistry::updateDeviceClass(std::uint8_t nodeId,
                                     std::uint8_t basicType,
                                     std::uint8_t genericType,
                                     std::uint8_t specificType) -> void
{
    withBoundHome(
        [&](const std::string& home) -> bool
        {
            const auto iter = state().nodes.find(nodeId);
            if (iter == state().nodes.end())
            {
                return false;
            }
            // Persistence intentionally goes through `persistAdd` rather
            // than a partial UPDATE — the schema is keyed by
            // (home_id, node_id) and `persistAdd` is an UPSERT, so a
            // single call covers both "row exists" (overwrite the three
            // fields, preserve commandClasses) and the unreachable-but-
            // safe "row missing" case.
            iter->second.basicType    = basicType;
            iter->second.genericType  = genericType;
            iter->second.specificType = specificType;
            persistAdd(home, iter->second);
            return true;
        });
}

auto NodeRegistry::snapshot() -> std::vector<NodeInfo>
{
    initIfNeeded();
    std::scoped_lock const lock(state().mutex);
    std::vector<NodeInfo> result;
    result.reserve(state().nodes.size());
    for (const auto& [_id, info] : state().nodes)
    {
        result.push_back(info);
    }
    return result;
}
