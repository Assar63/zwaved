#include "NodeMetadata.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"

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
#include <vector>

#include <sqlite3.h>

namespace
{
constexpr const char* DEFAULT_STATE_DIR = "/var/lib/zwaved";
constexpr const char* STATE_DIR_ENV     = "ZWAVED_STATE_DIR";
constexpr const char* DB_FILENAME       = "nodes.db";

constexpr const char* SCHEMA_SQL = R"(
CREATE TABLE IF NOT EXISTS node_metadata (
    home_id TEXT    NOT NULL,
    node_id INTEGER NOT NULL,
    key     TEXT    NOT NULL,
    value   TEXT    NOT NULL,
    PRIMARY KEY (home_id, node_id, key)
);
)";

constexpr const char* UPSERT_SQL     = "INSERT INTO node_metadata (home_id, node_id, key, value) VALUES (?, ?, ?, ?) "
                                       "ON CONFLICT(home_id, node_id, key) DO UPDATE SET value = excluded.value";
constexpr const char* DELETE_SQL     = "DELETE FROM node_metadata WHERE home_id = ? AND node_id = ? AND key = ?";
constexpr const char* SELECT_ONE_SQL = "SELECT value FROM node_metadata WHERE home_id = ? AND node_id = ? AND key = ?";
constexpr const char* SELECT_ALL_SQL =
    "SELECT key, value FROM node_metadata WHERE home_id = ? AND node_id = ? ORDER BY key";
constexpr const char* SELECT_NODES_BY_TAG_SQL =
    "SELECT node_id FROM node_metadata WHERE home_id = ? AND key = ? AND value = ? ORDER BY node_id ASC";

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

/// RAII wrapper around sqlite3_stmt* — same shape as the helpers in
/// NodeRegistry / PendingQueue / PolicyRegister, kept local rather than
/// shared (they're the only users today).
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
            Logger::error(std::string("[NodeMetadata] prepare ") + label + " failed: " + sqlite3_errmsg(database));
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
    auto step() -> int
    {
        return sqlite3_step(stmt_);
    }
    auto execDone() -> void
    {
        if (sqlite3_step(stmt_) != SQLITE_DONE)
        {
            Logger::error(std::string("[NodeMetadata] ") + label_ + " failed: " + sqlite3_errmsg(database_));
        }
    }
    [[nodiscard]] auto columnText(int col) const -> std::string
    {
        // sqlite3_column_text returns `const unsigned char*`; build the
        // string from the byte range to avoid a reinterpret_cast.
        const auto* text = sqlite3_column_text(stmt_, col);
        if (text == nullptr)
        {
            return {};
        }
        const int len = sqlite3_column_bytes(stmt_, col);
        return {text, text + len};
    }
    [[nodiscard]] auto columnInt(int col) const -> int
    {
        return sqlite3_column_int(stmt_, col);
    }

  private:
    sqlite3_stmt* stmt_ = nullptr;
    sqlite3* database_  = nullptr;
    const char* label_  = nullptr;
};
}  // namespace

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): pimpl, public members read like a struct
struct NodeMetadata::Store::State
{
    mutable std::mutex mutex;
    sqlite3* db = nullptr;
    std::optional<std::string> currentHomeId;

    State()                                    = default;
    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
    ~State()
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

NodeMetadata::Store::Store(const std::filesystem::path& dbPath)
    : state_(std::make_unique<State>())
{
    std::error_code errorCode;
    std::filesystem::create_directories(dbPath.parent_path(), errorCode);
    if (errorCode)
    {
        Logger::error("[NodeMetadata] cannot create state dir " + dbPath.parent_path().string() + ": " +
                      errorCode.message());
        return;
    }
    if (sqlite3_open(dbPath.c_str(), &state_->db) != SQLITE_OK)
    {
        Logger::error("[NodeMetadata] cannot open " + dbPath.string() + ": " + sqlite3_errmsg(state_->db));
        sqlite3_close(state_->db);
        state_->db = nullptr;
        return;
    }
    char* err = nullptr;
    if (sqlite3_exec(state_->db, SCHEMA_SQL, nullptr, nullptr, &err) != SQLITE_OK)
    {
        Logger::error(std::string("[NodeMetadata] CREATE TABLE failed: ") + (err != nullptr ? err : "?"));
        sqlite3_free(err);
        sqlite3_close(state_->db);
        state_->db = nullptr;
        return;
    }
    Logger::info("[NodeMetadata] db ready at " + dbPath.string());
}

NodeMetadata::Store::~Store() = default;

auto NodeMetadata::Store::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    const std::scoped_lock lock(state_->mutex);
    state_->currentHomeId = formatHomeId(homeIdBytes);
}

auto NodeMetadata::Store::set(std::uint8_t nodeId, const std::string& key, const std::string& value) -> void
{
    // Empty value is the "clear this key" idiom.
    if (value.empty())
    {
        remove(nodeId, key);
        return;
    }
    {
        const std::scoped_lock lock(state_->mutex);
        if (state_->db == nullptr || !state_->currentHomeId.has_value())
        {
            Logger::warn("[NodeMetadata] set dropped — no DB / no home (node " + std::to_string(nodeId) + ")");
            return;
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
        const std::string& home = *state_->currentHomeId;
        Stmt stmt(state_->db, UPSERT_SQL, "UPSERT");
        if (!stmt.valid())
        {
            return;
        }
        stmt.bindText(1, home).bindInt(2, nodeId).bindText(3, key).bindText(4, value).execDone();
    }
    MessageBus::publish(MessageBus::NodeMetadataChanged{.nodeId = nodeId});
}

auto NodeMetadata::Store::remove(std::uint8_t nodeId, const std::string& key) -> void
{
    {
        const std::scoped_lock lock(state_->mutex);
        if (state_->db == nullptr || !state_->currentHomeId.has_value())
        {
            return;
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
        const std::string& home = *state_->currentHomeId;
        Stmt stmt(state_->db, DELETE_SQL, "DELETE");
        if (!stmt.valid())
        {
            return;
        }
        stmt.bindText(1, home).bindInt(2, nodeId).bindText(3, key).execDone();
    }
    MessageBus::publish(MessageBus::NodeMetadataChanged{.nodeId = nodeId});
}

auto NodeMetadata::Store::get(std::uint8_t nodeId, const std::string& key) const -> std::optional<std::string>
{
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return std::nullopt;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, SELECT_ONE_SQL, "SELECT one");
    if (!stmt.valid())
    {
        return std::nullopt;
    }
    stmt.bindText(1, home).bindInt(2, nodeId).bindText(3, key);
    if (stmt.step() != SQLITE_ROW)
    {
        return std::nullopt;
    }
    return stmt.columnText(0);
}

auto NodeMetadata::Store::getAll(std::uint8_t nodeId) const -> std::vector<Entry>
{
    std::vector<Entry> out;
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return out;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, SELECT_ALL_SQL, "SELECT all");
    if (!stmt.valid())
    {
        return out;
    }
    stmt.bindText(1, home).bindInt(2, nodeId);
    while (stmt.step() == SQLITE_ROW)
    {
        out.push_back(Entry{.key = stmt.columnText(0), .value = stmt.columnText(1)});
    }
    return out;
}

auto NodeMetadata::Store::nodesWith(const std::string& key, const std::string& value) const -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return out;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, SELECT_NODES_BY_TAG_SQL, "SELECT nodes by tag");
    if (!stmt.valid())
    {
        return out;
    }
    stmt.bindText(1, home).bindText(2, key).bindText(3, value);
    while (stmt.step() == SQLITE_ROW)
    {
        out.push_back(static_cast<std::uint8_t>(stmt.columnInt(0)));
    }
    return out;
}

// ---- Production singleton --------------------------------------------

namespace
{
struct SingletonState
{
    std::string configuredStateDir;
    MessageBus::SubscriptionGuard storageSub;
    std::unique_ptr<NodeMetadata::Store> store;
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

auto NodeMetadata::instance() -> Store&
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
