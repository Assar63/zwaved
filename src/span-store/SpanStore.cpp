#include "SpanStore.hpp"

#include "../logger/Logger.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <sqlite3.h>

namespace
{
constexpr const char* CREATE_TABLE_SQL = "CREATE TABLE IF NOT EXISTS span_state ("
                                         "  home_id TEXT NOT NULL,"
                                         "  peer_node_id INTEGER NOT NULL,"
                                         "  state BLOB NOT NULL,"
                                         "  PRIMARY KEY (home_id, peer_node_id))";
constexpr const char* UPSERT_SQL = "INSERT OR REPLACE INTO span_state (home_id, peer_node_id, state) VALUES (?, ?, ?)";
constexpr const char* DELETE_SQL = "DELETE FROM span_state WHERE home_id = ? AND peer_node_id = ?";
constexpr const char* SELECT_SQL = "SELECT peer_node_id, state FROM span_state WHERE home_id = ?";

constexpr int BIND_HOME = 1;
constexpr int BIND_PEER = 2;
constexpr int BIND_BLOB = 3;
constexpr int COL_PEER  = 0;
constexpr int COL_STATE = 1;

// RAII wrapper around sqlite3_stmt* — mirrors the Stmt helper in PendingQueue.
class Stmt
{
  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): sql text vs log label are distinct roles
    Stmt(sqlite3* database, const char* sql, const char* label)
    {
        if (sqlite3_prepare_v2(database, sql, -1, &stmt_, nullptr) != SQLITE_OK)
        {
            Logger::error(std::string("[SpanStore] prepare ") + label + " failed: " + sqlite3_errmsg(database));
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
    auto bindBlob(int pos, const void* data, int size) -> Stmt&
    {
        sqlite3_bind_blob(stmt_, pos, data, size, SQLITE_TRANSIENT);
        return *this;
    }

  private:
    sqlite3_stmt* stmt_ = nullptr;
};

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

struct SpanStore::Store::State
{
    sqlite3* db = nullptr;
    std::optional<std::string> homeId;
};

SpanStore::Store::Store(const std::filesystem::path& dbPath)
    : state_(std::make_unique<State>())
{
    if (sqlite3_open(dbPath.c_str(), &state_->db) != SQLITE_OK)
    {
        Logger::error(std::string("[SpanStore] open failed: ") + sqlite3_errmsg(state_->db));
        sqlite3_close(state_->db);
        state_->db = nullptr;
        return;
    }
    char* err = nullptr;
    if (sqlite3_exec(state_->db, CREATE_TABLE_SQL, nullptr, nullptr, &err) != SQLITE_OK)
    {
        Logger::error(std::string("[SpanStore] CREATE TABLE failed: ") + (err != nullptr ? err : "?"));
        sqlite3_free(err);
    }
}

SpanStore::Store::~Store()
{
    if (state_ != nullptr)
    {
        sqlite3_close(state_->db);
    }
}

auto SpanStore::Store::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    state_->homeId = toHex(homeIdBytes);
}

auto SpanStore::Store::save(std::uint8_t peer, const S2::SPAN::InnerState& state) -> void
{
    const auto& home = state_->homeId;
    if (state_->db == nullptr || !home.has_value())
    {
        Logger::warn("[SpanStore] save ignored — no DB or home ID bound");
        return;
    }
    Stmt stmt(state_->db, UPSERT_SQL, "UPSERT");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(BIND_HOME, *home)
        .bindInt(BIND_PEER, peer)
        .bindBlob(BIND_BLOB, state.data(), static_cast<int>(state.size()));
    sqlite3_step(stmt.raw());
}

auto SpanStore::Store::remove(std::uint8_t peer) -> void
{
    const auto& home = state_->homeId;
    if (state_->db == nullptr || !home.has_value())
    {
        return;
    }
    Stmt stmt(state_->db, DELETE_SQL, "DELETE");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(BIND_HOME, *home).bindInt(BIND_PEER, peer);
    sqlite3_step(stmt.raw());
}

auto SpanStore::Store::loadAll() -> std::map<std::uint8_t, S2::SPAN::InnerState>
{
    std::map<std::uint8_t, S2::SPAN::InnerState> result;
    const auto& home = state_->homeId;
    if (state_->db == nullptr || !home.has_value())
    {
        return result;
    }
    Stmt stmt(state_->db, SELECT_SQL, "SELECT");
    if (!stmt.valid())
    {
        return result;
    }
    stmt.bindText(BIND_HOME, *home);
    while (sqlite3_step(stmt.raw()) == SQLITE_ROW)
    {
        const auto peer     = static_cast<std::uint8_t>(sqlite3_column_int(stmt.raw(), COL_PEER));
        const void* blob    = sqlite3_column_blob(stmt.raw(), COL_STATE);
        const int blobBytes = sqlite3_column_bytes(stmt.raw(), COL_STATE);
        if (blob == nullptr || blobBytes != static_cast<int>(std::tuple_size_v<S2::SPAN::InnerState>))
        {
            continue;  // skip a malformed row rather than corrupt a SPAN
        }
        S2::SPAN::InnerState inner{};
        std::copy_n(static_cast<const std::uint8_t*>(blob), inner.size(), inner.begin());
        result.emplace(peer, inner);
    }
    return result;
}
