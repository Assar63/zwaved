#include "NodeValues.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../sqlite/Sqlite.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sqlite3.h>

namespace
{
constexpr const char* DEFAULT_STATE_DIR = "/var/lib/zwaved";
constexpr const char* STATE_DIR_ENV     = "ZWAVED_STATE_DIR";
constexpr const char* DB_FILENAME       = "nodes.db";

constexpr const char* CREATE_TABLE_SQL = "CREATE TABLE IF NOT EXISTS node_values ("
                                         "  home_id TEXT NOT NULL,"
                                         "  node_id INTEGER NOT NULL,"
                                         "  value_id TEXT NOT NULL,"
                                         "  value TEXT NOT NULL,"
                                         "  updated_at INTEGER NOT NULL,"
                                         "  PRIMARY KEY (home_id, node_id, value_id))";
constexpr const char* UPSERT_SQL =
    "INSERT OR REPLACE INTO node_values (home_id, node_id, value_id, value, updated_at) VALUES (?, ?, ?, ?, ?)";
constexpr const char* SELECT_ONE_SQL =
    "SELECT value, updated_at FROM node_values WHERE home_id = ? AND node_id = ? AND value_id = ?";
constexpr const char* SELECT_ALL_SQL =
    "SELECT value_id, value, updated_at FROM node_values WHERE home_id = ? AND node_id = ? ORDER BY value_id";
constexpr const char* DELETE_NODE_SQL = "DELETE FROM node_values WHERE home_id = ? AND node_id = ?";

// Bind positions (1-based) for the UPSERT.
constexpr int BIND_HOME       = 1;
constexpr int BIND_NODE       = 2;
constexpr int BIND_VALUE_ID   = 3;
constexpr int BIND_VALUE      = 4;
constexpr int BIND_UPDATED_AT = 5;

constexpr std::uint8_t LOW_NIBBLE_MASK = 0x0F;
constexpr int NIBBLE_BITS              = 4;

auto toHex(const std::vector<std::uint8_t>& bytes) -> std::string
{
    static constexpr std::array<char, 16> hexDigits{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes)
    {
        out.push_back(hexDigits.at(byte >> NIBBLE_BITS));
        out.push_back(hexDigits.at(byte & LOW_NIBBLE_MASK));
    }
    return out;
}
}  // namespace

auto NodeValues::systemClock() -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

struct NodeValues::Store::State
{
    Sqlite::Db db;
    std::optional<std::string> homeId;
    Clock clock;
};

NodeValues::Store::Store(const std::filesystem::path& dbPath, Clock clock)
    : state_(std::make_unique<State>())
{
    state_->clock = std::move(clock);
    state_->db    = Sqlite::Db(dbPath, "NodeValues");
    if (!state_->db.valid())
    {
        return;
    }
    state_->db.exec(CREATE_TABLE_SQL, "CREATE TABLE");
}

NodeValues::Store::~Store() = default;

auto NodeValues::Store::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    state_->homeId = toHex(homeIdBytes);
}

auto NodeValues::Store::record(std::uint8_t nodeId, const std::string& valueId, const std::string& value) -> void
{
    const auto& home = state_->homeId;
    if (!state_->db.valid() || !home.has_value())
    {
        Logger::warn("[NodeValues] record ignored — no DB or home ID bound");
        return;
    }
    auto stmt = state_->db.prepare(UPSERT_SQL, "UPSERT");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(BIND_HOME, *home)
        .bindInt(BIND_NODE, nodeId)
        .bindText(BIND_VALUE_ID, valueId)
        .bindText(BIND_VALUE, value)
        .bindInt64(BIND_UPDATED_AT, state_->clock())
        .execDone();
}

auto NodeValues::Store::get(std::uint8_t nodeId, const std::string& valueId) const -> std::optional<Entry>
{
    const auto& home = state_->homeId;
    if (!state_->db.valid() || !home.has_value())
    {
        return std::nullopt;
    }
    auto stmt = state_->db.prepare(SELECT_ONE_SQL, "SELECT one");
    if (!stmt.valid())
    {
        return std::nullopt;
    }
    stmt.bindText(BIND_HOME, *home).bindInt(BIND_NODE, nodeId).bindText(BIND_VALUE_ID, valueId);
    if (stmt.step() != SQLITE_ROW)
    {
        return std::nullopt;
    }
    return Entry{.valueId = valueId, .value = stmt.columnText(0), .updatedAt = stmt.columnInt64(1)};
}

auto NodeValues::Store::getAll(std::uint8_t nodeId) const -> std::vector<Entry>
{
    std::vector<Entry> out;
    const auto& home = state_->homeId;
    if (!state_->db.valid() || !home.has_value())
    {
        return out;
    }
    auto stmt = state_->db.prepare(SELECT_ALL_SQL, "SELECT all");
    if (!stmt.valid())
    {
        return out;
    }
    stmt.bindText(BIND_HOME, *home).bindInt(BIND_NODE, nodeId);
    while (stmt.step() == SQLITE_ROW)
    {
        out.push_back(
            Entry{.valueId = stmt.columnText(0), .value = stmt.columnText(1), .updatedAt = stmt.columnInt64(2)});
    }
    return out;
}

auto NodeValues::Store::clearForNode(std::uint8_t nodeId) -> void
{
    const auto& home = state_->homeId;
    if (!state_->db.valid() || !home.has_value())
    {
        return;
    }
    auto stmt = state_->db.prepare(DELETE_NODE_SQL, "DELETE node");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(BIND_HOME, *home).bindInt(BIND_NODE, nodeId).execDone();
}

// ---- Production singleton --------------------------------------------

namespace
{
struct SingletonState
{
    std::string configuredStateDir;
    MessageBus::SubscriptionGuard storageSub;
    std::unique_ptr<NodeValues::Store> store;
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

auto NodeValues::instance() -> Store&
{
    std::call_once(singletonState().initFlag,
                   []
                   {
                       singletonState().storageSub =
                           MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::StorageConfig>(
                               [](const MessageBus::StorageConfig& cfg) -> void
                               { singletonState().configuredStateDir = cfg.stateDir; }));
                       singletonState().store = std::make_unique<Store>(resolveDbPath());
                   });
    return *singletonState().store;
}
