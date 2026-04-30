#include "NodeRegistry.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
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

struct State
{
    std::mutex mutex;
    std::map<std::uint8_t, NodeRegistry::NodeInfo> nodes;
    std::optional<std::string> currentHomeId;  // hex form, e.g. "E2A1B07C"
    sqlite3* db = nullptr;
    std::once_flag initFlag;
};

auto state() -> State&
{
    static State instance;
    return instance;
}

auto resolveDbPath() -> std::filesystem::path
{
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
    sqlite3_stmt* stmt = nullptr;
    int result         = 0;
    if (sqlite3_prepare_v2(database, "PRAGMA user_version", -1, &stmt, nullptr) != SQLITE_OK)
    {
        return 0;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        result = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SQL and label are clearly distinct at call sites
auto execOrLog(sqlite3* database, const char* sql, const char* what) -> bool
{
    char* err = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &err) != SQLITE_OK)
    {
        std::cerr << "[NodeRegistry] " << what << " failed: " << (err != nullptr ? err : "?") << '\n';
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
    std::cout << "[NodeRegistry] migrated schema to version " << CURRENT_SCHEMA_VERSION << '\n';
    return true;
}

auto loadNodesForHome(sqlite3* database, const std::string& homeId) -> std::map<std::uint8_t, NodeRegistry::NodeInfo>
{
    std::map<std::uint8_t, NodeRegistry::NodeInfo> result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(database, SELECT_FOR_HOME_SQL, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "[NodeRegistry] prepare SELECT failed: " << sqlite3_errmsg(database) << '\n';
        return result;
    }
    sqlite3_bind_text(stmt, 1, homeId.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        NodeRegistry::NodeInfo info;
        info.nodeId       = static_cast<std::uint8_t>(sqlite3_column_int(stmt, COL_NODE_ID));
        info.basicType    = static_cast<std::uint8_t>(sqlite3_column_int(stmt, COL_BASIC));
        info.genericType  = static_cast<std::uint8_t>(sqlite3_column_int(stmt, COL_GENERIC));
        info.specificType = static_cast<std::uint8_t>(sqlite3_column_int(stmt, COL_SPECIFIC));

        const auto* blob   = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, COL_CCS));
        const int blobSize = sqlite3_column_bytes(stmt, COL_CCS);
        if (blob != nullptr && blobSize > 0)
        {
            info.commandClasses.assign(blob, blob + blobSize);
        }
        result.emplace(info.nodeId, info);
    }
    sqlite3_finalize(stmt);
    return result;
}

auto initIfNeeded() -> void
{
    std::call_once(state().initFlag,
                   []
                   {
                       const auto path = resolveDbPath();
                       std::error_code errorCode;
                       std::filesystem::create_directories(path.parent_path(), errorCode);
                       if (errorCode)
                       {
                           std::cerr << "[NodeRegistry] cannot create state dir " << path.parent_path() << ": "
                                     << errorCode.message() << " — falling back to in-memory only\n";
                           return;
                       }
                       if (sqlite3_open(path.c_str(), &state().db) != SQLITE_OK)
                       {
                           std::cerr << "[NodeRegistry] cannot open " << path << ": " << sqlite3_errmsg(state().db)
                                     << " — falling back to in-memory only\n";
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
                       std::cout << "[NodeRegistry] db ready at " << path << '\n';
                   });
}

auto persistAdd(const std::string& homeId, const NodeRegistry::NodeInfo& info) -> void
{
    if (state().db == nullptr)
    {
        return;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(state().db, UPSERT_SQL, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "[NodeRegistry] prepare UPSERT failed: " << sqlite3_errmsg(state().db) << '\n';
        return;
    }
    sqlite3_bind_text(stmt, BIND_HOME, homeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, BIND_NODE_ID, info.nodeId);
    sqlite3_bind_int(stmt, BIND_BASIC, info.basicType);
    sqlite3_bind_int(stmt, BIND_GENERIC, info.genericType);
    sqlite3_bind_int(stmt, BIND_SPECIFIC, info.specificType);
    sqlite3_bind_blob(
        stmt, BIND_CCS, info.commandClasses.data(), static_cast<int>(info.commandClasses.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        std::cerr << "[NodeRegistry] UPSERT failed: " << sqlite3_errmsg(state().db) << '\n';
    }
    sqlite3_finalize(stmt);
}

auto persistRemove(const std::string& homeId, std::uint8_t nodeId) -> void
{
    if (state().db == nullptr)
    {
        return;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(state().db, DELETE_SQL, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "[NodeRegistry] prepare DELETE failed: " << sqlite3_errmsg(state().db) << '\n';
        return;
    }
    sqlite3_bind_text(stmt, 1, homeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, nodeId);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        std::cerr << "[NodeRegistry] DELETE failed: " << sqlite3_errmsg(state().db) << '\n';
    }
    sqlite3_finalize(stmt);
}
}  // namespace

auto NodeRegistry::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    initIfNeeded();
    auto homeIdStr = formatHomeId(homeIdBytes);
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
    std::cout << "[NodeRegistry] bound to home " << homeIdStr << " (" << state().nodes.size() << " node(s) loaded)\n";
}

auto NodeRegistry::add(const NodeInfo& info) -> void
{
    initIfNeeded();
    std::scoped_lock const lock(state().mutex);
    const auto home = state().currentHomeId;
    if (!home.has_value())
    {
        return;
    }
    state().nodes[info.nodeId] = info;
    persistAdd(*home, info);
}

auto NodeRegistry::remove(std::uint8_t nodeId) -> void
{
    initIfNeeded();
    std::scoped_lock const lock(state().mutex);
    const auto home = state().currentHomeId;
    if (!home.has_value())
    {
        return;
    }
    state().nodes.erase(nodeId);
    persistRemove(*home, nodeId);
}

auto NodeRegistry::seed(std::uint8_t nodeId) -> void
{
    initIfNeeded();
    std::scoped_lock const lock(state().mutex);
    const auto home = state().currentHomeId;
    if (!home.has_value())
    {
        return;
    }
    if (state().nodes.find(nodeId) != state().nodes.end())
    {
        return;
    }
    NodeInfo info;
    info.nodeId           = nodeId;
    state().nodes[nodeId] = info;
    persistAdd(*home, info);
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
