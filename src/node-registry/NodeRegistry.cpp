#include "NodeRegistry.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include <sqlite3.h>

namespace
{
constexpr const char* DEFAULT_STATE_DIR = "/var/lib/zwaved";
constexpr const char* STATE_DIR_ENV     = "ZWAVED_STATE_DIR";
constexpr const char* DB_FILENAME       = "nodes.db";

constexpr const char* SCHEMA_SQL = R"(
CREATE TABLE IF NOT EXISTS nodes (
    node_id         INTEGER PRIMARY KEY,
    basic_type      INTEGER NOT NULL,
    generic_type    INTEGER NOT NULL,
    specific_type   INTEGER NOT NULL,
    command_classes BLOB
)
)";

constexpr const char* UPSERT_SQL =
    "INSERT INTO nodes (node_id, basic_type, generic_type, specific_type, command_classes) "
    "VALUES (?, ?, ?, ?, ?) "
    "ON CONFLICT(node_id) DO UPDATE SET "
    "basic_type      = excluded.basic_type, "
    "generic_type    = excluded.generic_type, "
    "specific_type   = excluded.specific_type, "
    "command_classes = excluded.command_classes";

constexpr const char* DELETE_SQL = "DELETE FROM nodes WHERE node_id = ?";

constexpr const char* SELECT_ALL_SQL =
    "SELECT node_id, basic_type, generic_type, specific_type, command_classes FROM nodes";

// SQL parameter positions (1-based per sqlite3_bind_*).
constexpr int BIND_NODE_ID  = 1;
constexpr int BIND_BASIC    = 2;
constexpr int BIND_GENERIC  = 3;
constexpr int BIND_SPECIFIC = 4;
constexpr int BIND_CCS      = 5;

// SELECT column indices (0-based per sqlite3_column_*).
constexpr int COL_NODE_ID  = 0;
constexpr int COL_BASIC    = 1;
constexpr int COL_GENERIC  = 2;
constexpr int COL_SPECIFIC = 3;
constexpr int COL_CCS      = 4;

// Process exit closes the database implicitly via the kernel cleaning
// up the file descriptor; SQLite leaves no dirty pages once each
// statement commits (default journaling, synchronous=FULL), so we
// don't bother with a destructor — keeping State a plain aggregate
// satisfies misc-non-private-member-variables-in-classes.
struct State
{
    std::mutex mutex;
    std::map<std::uint8_t, NodeRegistry::NodeInfo> nodes;
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

auto loadNodes(sqlite3* database) -> std::map<std::uint8_t, NodeRegistry::NodeInfo>
{
    std::map<std::uint8_t, NodeRegistry::NodeInfo> result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(database, SELECT_ALL_SQL, -1, &stmt, nullptr) != SQLITE_OK)
    {
        std::cerr << "[NodeRegistry] prepare SELECT failed: " << sqlite3_errmsg(database) << '\n';
        return result;
    }
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
    std::call_once(
        state().initFlag,
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
            char* schemaErr = nullptr;
            if (sqlite3_exec(state().db, SCHEMA_SQL, nullptr, nullptr, &schemaErr) != SQLITE_OK)
            {
                std::cerr << "[NodeRegistry] schema setup failed: " << (schemaErr != nullptr ? schemaErr : "?") << '\n';
                sqlite3_free(schemaErr);
                sqlite3_close(state().db);
                state().db = nullptr;
                return;
            }
            state().nodes = loadNodes(state().db);
            std::cout << "[NodeRegistry] loaded " << state().nodes.size() << " node(s) from " << path << '\n';
        });
}

auto persistAdd(const NodeRegistry::NodeInfo& info) -> void
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

auto persistRemove(std::uint8_t nodeId) -> void
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
    sqlite3_bind_int(stmt, BIND_NODE_ID, nodeId);
    if (sqlite3_step(stmt) != SQLITE_DONE)
    {
        std::cerr << "[NodeRegistry] DELETE failed: " << sqlite3_errmsg(state().db) << '\n';
    }
    sqlite3_finalize(stmt);
}
}  // namespace

auto NodeRegistry::add(const NodeInfo& info) -> void
{
    initIfNeeded();
    std::scoped_lock const lock(state().mutex);
    state().nodes[info.nodeId] = info;
    persistAdd(info);
}

auto NodeRegistry::remove(std::uint8_t nodeId) -> void
{
    initIfNeeded();
    std::scoped_lock const lock(state().mutex);
    state().nodes.erase(nodeId);
    persistRemove(nodeId);
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
