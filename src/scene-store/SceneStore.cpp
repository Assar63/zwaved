#include "SceneStore.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"

#include <cstddef>
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

// Schema version stamped into PRAGMA user_version. v1 added the `source`
// discriminator column to scene_triggers (#124); a v0 DB (created by #120,
// before #124) is migrated in the constructor.
constexpr int SCHEMA_VERSION = 1;

constexpr const char* SCHEMA_SCENES_SQL = R"(
CREATE TABLE IF NOT EXISTS scenes (
    home_id  TEXT NOT NULL,
    scene_id TEXT NOT NULL,
    actions  BLOB NOT NULL,
    PRIMARY KEY (home_id, scene_id)
);
)";

// v1 trigger table: `source` (SOURCE_* discriminator) is part of the key so
// Central Scene / Basic Set / Scene Activation triggers can share the same
// (node, selector) without colliding.
constexpr const char* SCHEMA_TRIGGERS_SQL = R"(
CREATE TABLE IF NOT EXISTS scene_triggers (
    home_id        TEXT    NOT NULL,
    source         INTEGER NOT NULL,
    source_node_id INTEGER NOT NULL,
    scene_number   INTEGER NOT NULL,
    key_attribute  INTEGER NOT NULL,
    scene_id       TEXT    NOT NULL,
    PRIMARY KEY (home_id, source, source_node_id, scene_number, key_attribute)
);
)";

constexpr const char* UPSERT_SCENE_SQL = "INSERT OR REPLACE INTO scenes (home_id, scene_id, actions) VALUES (?, ?, ?)";
constexpr const char* SELECT_SCENE_SQL = "SELECT actions FROM scenes WHERE home_id = ? AND scene_id = ?";
constexpr const char* DELETE_SCENE_SQL = "DELETE FROM scenes WHERE home_id = ? AND scene_id = ?";
constexpr const char* LIST_SCENES_SQL  = "SELECT scene_id FROM scenes WHERE home_id = ? ORDER BY scene_id ASC";

constexpr const char* UPSERT_TRIGGER_SQL  = "INSERT OR REPLACE INTO scene_triggers "
                                            "(home_id, source, source_node_id, scene_number, key_attribute, scene_id) "
                                            "VALUES (?, ?, ?, ?, ?, ?)";
constexpr const char* DELETE_TRIGGER_SQL  = "DELETE FROM scene_triggers WHERE home_id = ? AND source = ? "
                                            "AND source_node_id = ? AND scene_number = ? AND key_attribute = ?";
constexpr const char* RESOLVE_TRIGGER_SQL = "SELECT scene_id FROM scene_triggers WHERE home_id = ? AND source = ? "
                                            "AND source_node_id = ? AND scene_number = ? AND key_attribute = ?";
constexpr const char* LIST_TRIGGERS_SQL   = "SELECT source, source_node_id, scene_number, key_attribute, scene_id "
                                            "FROM scene_triggers WHERE home_id = ? "
                                            "ORDER BY source ASC, source_node_id ASC, scene_number ASC, "
                                            "key_attribute ASC";

// v0 -> v1 migration: copy legacy rows (Central Scene only) into the v1
// table, stamping source = SOURCE_CENTRAL_SCENE (0).
constexpr const char* MIGRATE_RENAME_SQL = "ALTER TABLE scene_triggers RENAME TO scene_triggers_legacy";
constexpr const char* MIGRATE_COPY_SQL =
    "INSERT OR IGNORE INTO scene_triggers (home_id, source, source_node_id, scene_number, key_attribute, scene_id) "
    "SELECT home_id, 0, source_node_id, scene_number, key_attribute, scene_id FROM scene_triggers_legacy";
constexpr const char* MIGRATE_DROP_SQL    = "DROP TABLE scene_triggers_legacy";
constexpr const char* LEGACY_EXISTS_SQL   = "SELECT 1 FROM sqlite_master WHERE type='table' AND name='scene_triggers'";
constexpr const char* READ_USER_VERSION   = "PRAGMA user_version";
constexpr const char* SET_USER_VERSION_V1 = "PRAGMA user_version = 1";

// 1-based bind positions for the 6-column trigger upsert (named so the 5th /
// 6th stay out of the magic-number checker; positions 1-4 are small enough
// to be left as literals in the other statements).
constexpr int BIND_TRIGGER_HOME     = 1;
constexpr int BIND_TRIGGER_SOURCE   = 2;
constexpr int BIND_TRIGGER_NODE     = 3;
constexpr int BIND_TRIGGER_SCENE_NO = 4;
constexpr int BIND_TRIGGER_KEY      = 5;
constexpr int BIND_TRIGGER_SCENE_ID = 6;

// Versioned, length-prefixed serialization of a scene's action list (same
// idiom as PolicyRegister's policy blob — the leading version byte makes a
// format change detectable). Layout: [version][count] then per action
// [targetNodeId][payloadLen][payload bytes]. count / payloadLen are single
// bytes (a scene won't exceed 255 actions, nor an action 255 payload bytes).
constexpr std::uint8_t BLOB_VERSION  = 1;
constexpr std::size_t MAX_BLOB_ITEMS = 255;

auto encodeActions(const std::vector<SceneStore::Action>& actions) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    out.push_back(BLOB_VERSION);
    out.push_back(static_cast<std::uint8_t>(actions.size()));
    for (const auto& action : actions)
    {
        out.push_back(action.targetNodeId);
        out.push_back(static_cast<std::uint8_t>(action.ccPayload.size()));
        out.insert(out.end(), action.ccPayload.begin(), action.ccPayload.end());
    }
    return out;
}

auto decodeActions(const std::uint8_t* data, std::size_t size) -> std::optional<std::vector<SceneStore::Action>>
{
    if (data == nullptr || size < 2 || data[0] != BLOB_VERSION)
    {
        return std::nullopt;
    }
    std::size_t pos          = 1;
    const std::uint8_t count = data[pos++];
    std::vector<SceneStore::Action> out;
    for (std::uint8_t idx = 0; idx < count; ++idx)
    {
        if (pos + 2 > size)
        {
            return std::nullopt;
        }
        SceneStore::Action action;
        action.targetNodeId       = data[pos++];
        const std::uint8_t length = data[pos++];
        if (pos + length > size)
        {
            return std::nullopt;
        }
        action.ccPayload.assign(data + pos, data + pos + length);
        pos += length;
        out.push_back(std::move(action));
    }
    return out;
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

/// RAII wrapper around `sqlite3_stmt*` (mirrors the helper in PendingQueue /
/// NodeRegistry — intentionally not extracted into a shared header).
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
            Logger::error(std::string("[SceneStore] prepare ") + label + " failed: " + sqlite3_errmsg(database));
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
    auto execDone() -> void
    {
        if (sqlite3_step(stmt_) != SQLITE_DONE)
        {
            Logger::error(std::string("[SceneStore] ") + label_ + " failed: " + sqlite3_errmsg(database_));
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

/// Run one bare SQL statement, logging on failure. Returns success.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SQL and label are clearly distinct at call sites
auto execSql(sqlite3* database, const char* sql, const char* label) -> bool
{
    char* err = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &err) != SQLITE_OK)
    {
        Logger::error(std::string("[SceneStore] ") + label + " failed: " + (err != nullptr ? err : "?"));
        sqlite3_free(err);
        return false;
    }
    return true;
}

/// Create the schema and migrate a pre-#124 (v0) trigger table to v1 (adds
/// the `source` discriminator). Idempotent: a fresh DB gets the v1 tables
/// directly; an already-migrated DB (user_version == 1) is left untouched.
auto runSchema(sqlite3* database) -> bool
{
    if (!execSql(database, SCHEMA_SCENES_SQL, "CREATE scenes"))
    {
        return false;
    }
    int userVersion = 0;
    {
        Stmt stmt(database, READ_USER_VERSION, "read user_version");
        if (stmt.valid() && stmt.step() == SQLITE_ROW)
        {
            userVersion = sqlite3_column_int(stmt.raw(), 0);
        }
    }
    if (userVersion >= SCHEMA_VERSION)
    {
        // Already migrated — the v1 scene_triggers table is present.
        return true;
    }
    // Does a legacy (v0) scene_triggers table exist to migrate from?
    bool legacy = false;
    {
        Stmt stmt(database, LEGACY_EXISTS_SQL, "check legacy triggers");
        legacy = stmt.valid() && stmt.step() == SQLITE_ROW;
    }
    if (legacy)
    {
        if (!execSql(database, MIGRATE_RENAME_SQL, "rename legacy triggers") ||
            !execSql(database, SCHEMA_TRIGGERS_SQL, "CREATE scene_triggers") ||
            !execSql(database, MIGRATE_COPY_SQL, "copy legacy triggers") ||
            !execSql(database, MIGRATE_DROP_SQL, "drop legacy triggers"))
        {
            return false;
        }
        Logger::info("[SceneStore] migrated scene_triggers to v1 (added source discriminator)");
    }
    else if (!execSql(database, SCHEMA_TRIGGERS_SQL, "CREATE scene_triggers"))
    {
        return false;
    }
    return execSql(database, SET_USER_VERSION_V1, "set user_version");
}
}  // namespace

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): pimpl, public members read like a struct
struct SceneStore::Store::State
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

SceneStore::Store::Store(const std::filesystem::path& dbPath)
    : state_(std::make_unique<State>())
{
    std::error_code errorCode;
    std::filesystem::create_directories(dbPath.parent_path(), errorCode);
    if (errorCode)
    {
        Logger::error("[SceneStore] cannot create state dir " + dbPath.parent_path().string() + ": " +
                      errorCode.message());
        return;
    }
    if (sqlite3_open(dbPath.c_str(), &state_->db) != SQLITE_OK)
    {
        Logger::error("[SceneStore] cannot open " + dbPath.string() + ": " + sqlite3_errmsg(state_->db));
        sqlite3_close(state_->db);
        state_->db = nullptr;
        return;
    }
    if (!runSchema(state_->db))
    {
        sqlite3_close(state_->db);
        state_->db = nullptr;
        return;
    }
    Logger::info("[SceneStore] db ready at " + dbPath.string());
}

SceneStore::Store::~Store() = default;

auto SceneStore::Store::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    const std::scoped_lock lock(state_->mutex);
    state_->currentHomeId = formatHomeId(homeIdBytes);
}

auto SceneStore::Store::setScene(const std::string& sceneId, const std::vector<Action>& actions) -> void
{
    if (actions.size() > MAX_BLOB_ITEMS)
    {
        Logger::warn("[SceneStore] setScene '" + sceneId + "' dropped — too many actions");
        return;
    }
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        Logger::warn("[SceneStore] setScene dropped — no DB / no home");
        return;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    const auto blob         = encodeActions(actions);
    Stmt stmt(state_->db, UPSERT_SCENE_SQL, "UPSERT scene");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(1, home).bindText(2, sceneId).bindBlob(3, blob.data(), static_cast<int>(blob.size())).execDone();
}

auto SceneStore::Store::getScene(const std::string& sceneId) const -> std::optional<std::vector<Action>>
{
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return std::nullopt;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, SELECT_SCENE_SQL, "SELECT scene");
    if (!stmt.valid())
    {
        return std::nullopt;
    }
    stmt.bindText(1, home).bindText(2, sceneId);
    if (stmt.step() != SQLITE_ROW)
    {
        return std::nullopt;
    }
    const auto* blob   = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt.raw(), 0));
    const int blobSize = sqlite3_column_bytes(stmt.raw(), 0);
    return decodeActions(blob, static_cast<std::size_t>(blobSize));
}

auto SceneStore::Store::deleteScene(const std::string& sceneId) -> void
{
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, DELETE_SCENE_SQL, "DELETE scene");
    if (stmt.valid())
    {
        stmt.bindText(1, home).bindText(2, sceneId).execDone();
    }
}

auto SceneStore::Store::listScenes() const -> std::vector<std::string>
{
    std::vector<std::string> out;
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return out;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, LIST_SCENES_SQL, "LIST scenes");
    if (!stmt.valid())
    {
        return out;
    }
    stmt.bindText(1, home);
    while (stmt.step() == SQLITE_ROW)
    {
        const auto* text = sqlite3_column_text(stmt.raw(), 0);
        if (text != nullptr)
        {
            out.emplace_back(
                reinterpret_cast<const char*>(text));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        }
    }
    return out;
}

auto SceneStore::Store::bindTrigger(std::uint8_t source,
                                    std::uint8_t sourceNodeId,
                                    std::uint8_t sceneNumber,
                                    std::uint8_t keyAttribute,
                                    const std::string& sceneId) -> void
{
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        Logger::warn("[SceneStore] bindTrigger dropped — no DB / no home");
        return;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, UPSERT_TRIGGER_SQL, "UPSERT trigger");
    if (stmt.valid())
    {
        stmt.bindText(BIND_TRIGGER_HOME, home)
            .bindInt(BIND_TRIGGER_SOURCE, source)
            .bindInt(BIND_TRIGGER_NODE, sourceNodeId)
            .bindInt(BIND_TRIGGER_SCENE_NO, sceneNumber)
            .bindInt(BIND_TRIGGER_KEY, keyAttribute)
            .bindText(BIND_TRIGGER_SCENE_ID, sceneId)
            .execDone();
    }
}

auto SceneStore::Store::unbindTrigger(std::uint8_t source,
                                      std::uint8_t sourceNodeId,
                                      std::uint8_t sceneNumber,
                                      std::uint8_t keyAttribute) -> void
{
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, DELETE_TRIGGER_SQL, "DELETE trigger");
    if (stmt.valid())
    {
        stmt.bindText(BIND_TRIGGER_HOME, home)
            .bindInt(BIND_TRIGGER_SOURCE, source)
            .bindInt(BIND_TRIGGER_NODE, sourceNodeId)
            .bindInt(BIND_TRIGGER_SCENE_NO, sceneNumber)
            .bindInt(BIND_TRIGGER_KEY, keyAttribute)
            .execDone();
    }
}

auto SceneStore::Store::resolveTrigger(std::uint8_t source,
                                       std::uint8_t sourceNodeId,
                                       std::uint8_t sceneNumber,
                                       std::uint8_t keyAttribute) const -> std::optional<std::string>
{
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return std::nullopt;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, RESOLVE_TRIGGER_SQL, "RESOLVE trigger");
    if (!stmt.valid())
    {
        return std::nullopt;
    }
    stmt.bindText(BIND_TRIGGER_HOME, home)
        .bindInt(BIND_TRIGGER_SOURCE, source)
        .bindInt(BIND_TRIGGER_NODE, sourceNodeId)
        .bindInt(BIND_TRIGGER_SCENE_NO, sceneNumber)
        .bindInt(BIND_TRIGGER_KEY, keyAttribute);
    if (stmt.step() != SQLITE_ROW)
    {
        return std::nullopt;
    }
    const auto* text = sqlite3_column_text(stmt.raw(), 0);
    if (text == nullptr)
    {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(text));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}

auto SceneStore::Store::listTriggers() const -> std::vector<Trigger>
{
    std::vector<Trigger> out;
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return out;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, LIST_TRIGGERS_SQL, "LIST triggers");
    if (!stmt.valid())
    {
        return out;
    }
    stmt.bindText(1, home);
    while (stmt.step() == SQLITE_ROW)
    {
        Trigger trigger;
        trigger.source       = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), 0));
        trigger.sourceNodeId = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), 1));
        trigger.sceneNumber  = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), 2));
        trigger.keyAttribute = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), 3));
        const auto* text     = sqlite3_column_text(stmt.raw(), 4);
        if (text != nullptr)
        {
            trigger.sceneId =
                reinterpret_cast<const char*>(text);  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        }
        out.push_back(std::move(trigger));
    }
    return out;
}

// ---- Production singleton ---------------------------------------

namespace
{
struct SingletonState
{
    std::string configuredStateDir;
    MessageBus::SubscriptionGuard storageSub;
    std::unique_ptr<SceneStore::Store> store;
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

auto SceneStore::instance() -> Store&
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
