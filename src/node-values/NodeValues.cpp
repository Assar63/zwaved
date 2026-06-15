#include "NodeValues.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"

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

// RAII wrapper around sqlite3_stmt* — mirrors the Stmt helper in the sibling
// stores (PendingQueue / NodeMetadata / SpanStore).
class Stmt
{
  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): sql text vs log label are distinct roles
    Stmt(sqlite3* database, const char* sql, const char* label)
    {
        if (sqlite3_prepare_v2(database, sql, -1, &stmt_, nullptr) != SQLITE_OK)
        {
            Logger::error(std::string("[NodeValues] prepare ") + label + " failed: " + sqlite3_errmsg(database));
            stmt_ = nullptr;
        }
    }
    ~Stmt()
    {
        sqlite3_finalize(stmt_);
    }
    Stmt(const Stmt&)                        = delete;
    auto operator=(const Stmt&) -> Stmt&     = delete;
    Stmt(Stmt&&) noexcept                    = delete;
    auto operator=(Stmt&&) noexcept -> Stmt& = delete;

    [[nodiscard]] auto valid() const -> bool
    {
        return stmt_ != nullptr;
    }
    [[nodiscard]] auto raw() const -> sqlite3_stmt*
    {
        return stmt_;
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
    auto bindInt64(int pos, std::int64_t value) -> Stmt&
    {
        sqlite3_bind_int64(stmt_, pos, value);
        return *this;
    }
    [[nodiscard]] auto columnText(int col) const -> std::string
    {
        // sqlite3_column_text returns `const unsigned char*`; build the string
        // from the byte range to avoid a reinterpret_cast (mirrors NodeMetadata).
        const auto* text = sqlite3_column_text(stmt_, col);
        if (text == nullptr)
        {
            return {};
        }
        const int len = sqlite3_column_bytes(stmt_, col);
        return {text, text + len};
    }
    [[nodiscard]] auto columnInt64(int col) const -> std::int64_t
    {
        return sqlite3_column_int64(stmt_, col);
    }

  private:
    sqlite3_stmt* stmt_ = nullptr;
};

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
    sqlite3* db = nullptr;
    std::optional<std::string> homeId;
    Clock clock;
};

NodeValues::Store::Store(const std::filesystem::path& dbPath, Clock clock)
    : state_(std::make_unique<State>())
{
    state_->clock = std::move(clock);
    if (sqlite3_open(dbPath.c_str(), &state_->db) != SQLITE_OK)
    {
        Logger::error(std::string("[NodeValues] open failed: ") + sqlite3_errmsg(state_->db));
        sqlite3_close(state_->db);
        state_->db = nullptr;
        return;
    }
    char* err = nullptr;
    if (sqlite3_exec(state_->db, CREATE_TABLE_SQL, nullptr, nullptr, &err) != SQLITE_OK)
    {
        Logger::error(std::string("[NodeValues] CREATE TABLE failed: ") + (err != nullptr ? err : "?"));
        sqlite3_free(err);
    }
}

NodeValues::Store::~Store()
{
    if (state_ != nullptr)
    {
        sqlite3_close(state_->db);
    }
}

auto NodeValues::Store::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    state_->homeId = toHex(homeIdBytes);
}

auto NodeValues::Store::record(std::uint8_t nodeId, const std::string& valueId, const std::string& value) -> void
{
    const auto& home = state_->homeId;
    if (state_->db == nullptr || !home.has_value())
    {
        Logger::warn("[NodeValues] record ignored — no DB or home ID bound");
        return;
    }
    Stmt stmt(state_->db, UPSERT_SQL, "UPSERT");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(BIND_HOME, *home)
        .bindInt(BIND_NODE, nodeId)
        .bindText(BIND_VALUE_ID, valueId)
        .bindText(BIND_VALUE, value)
        .bindInt64(BIND_UPDATED_AT, state_->clock());
    sqlite3_step(stmt.raw());
}

auto NodeValues::Store::get(std::uint8_t nodeId, const std::string& valueId) const -> std::optional<Entry>
{
    const auto& home = state_->homeId;
    if (state_->db == nullptr || !home.has_value())
    {
        return std::nullopt;
    }
    Stmt stmt(state_->db, SELECT_ONE_SQL, "SELECT one");
    if (!stmt.valid())
    {
        return std::nullopt;
    }
    stmt.bindText(BIND_HOME, *home).bindInt(BIND_NODE, nodeId).bindText(BIND_VALUE_ID, valueId);
    if (sqlite3_step(stmt.raw()) != SQLITE_ROW)
    {
        return std::nullopt;
    }
    return Entry{.valueId = valueId, .value = stmt.columnText(0), .updatedAt = stmt.columnInt64(1)};
}

auto NodeValues::Store::getAll(std::uint8_t nodeId) const -> std::vector<Entry>
{
    std::vector<Entry> out;
    const auto& home = state_->homeId;
    if (state_->db == nullptr || !home.has_value())
    {
        return out;
    }
    Stmt stmt(state_->db, SELECT_ALL_SQL, "SELECT all");
    if (!stmt.valid())
    {
        return out;
    }
    stmt.bindText(BIND_HOME, *home).bindInt(BIND_NODE, nodeId);
    while (sqlite3_step(stmt.raw()) == SQLITE_ROW)
    {
        out.push_back(
            Entry{.valueId = stmt.columnText(0), .value = stmt.columnText(1), .updatedAt = stmt.columnInt64(2)});
    }
    return out;
}

auto NodeValues::Store::clearForNode(std::uint8_t nodeId) -> void
{
    const auto& home = state_->homeId;
    if (state_->db == nullptr || !home.has_value())
    {
        return;
    }
    Stmt stmt(state_->db, DELETE_NODE_SQL, "DELETE node");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(BIND_HOME, *home).bindInt(BIND_NODE, nodeId);
    sqlite3_step(stmt.raw());
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
