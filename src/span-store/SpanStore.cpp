#include "SpanStore.hpp"

#include "../logger/Logger.hpp"
#include "../sqlite/Sqlite.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
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
    Sqlite::Db db;
    std::optional<std::string> homeId;
};

SpanStore::Store::Store(const std::filesystem::path& dbPath)
    : state_(std::make_unique<State>())
{
    state_->db = Sqlite::Db(dbPath, "SpanStore");
    if (!state_->db.valid())
    {
        return;
    }
    state_->db.exec(CREATE_TABLE_SQL, "CREATE TABLE");
}

SpanStore::Store::~Store() = default;

auto SpanStore::Store::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    state_->homeId = toHex(homeIdBytes);
}

auto SpanStore::Store::save(std::uint8_t peer, const S2::SPAN::InnerState& state) -> void
{
    const auto& home = state_->homeId;
    if (!state_->db.valid() || !home.has_value())
    {
        Logger::warn("[SpanStore] save ignored — no DB or home ID bound");
        return;
    }
    auto stmt = state_->db.prepare(UPSERT_SQL, "UPSERT");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(BIND_HOME, *home)
        .bindInt(BIND_PEER, peer)
        .bindBlob(BIND_BLOB, state.data(), static_cast<int>(state.size()))
        .execDone();
}

auto SpanStore::Store::remove(std::uint8_t peer) -> void
{
    const auto& home = state_->homeId;
    if (!state_->db.valid() || !home.has_value())
    {
        return;
    }
    auto stmt = state_->db.prepare(DELETE_SQL, "DELETE");
    if (!stmt.valid())
    {
        return;
    }
    stmt.bindText(BIND_HOME, *home).bindInt(BIND_PEER, peer).execDone();
}

auto SpanStore::Store::loadAll() -> std::map<std::uint8_t, S2::SPAN::InnerState>
{
    std::map<std::uint8_t, S2::SPAN::InnerState> result;
    const auto& home = state_->homeId;
    if (!state_->db.valid() || !home.has_value())
    {
        return result;
    }
    auto stmt = state_->db.prepare(SELECT_SQL, "SELECT");
    if (!stmt.valid())
    {
        return result;
    }
    stmt.bindText(BIND_HOME, *home);
    while (stmt.step() == SQLITE_ROW)
    {
        const auto peer = static_cast<std::uint8_t>(stmt.columnInt(COL_PEER));
        const auto blob = stmt.columnBlob(COL_STATE);
        if (blob.size() != std::tuple_size_v<S2::SPAN::InnerState>)
        {
            continue;  // skip a malformed row rather than corrupt a SPAN
        }
        S2::SPAN::InnerState inner{};
        std::copy_n(blob.begin(), inner.size(), inner.begin());
        result.emplace(peer, inner);
    }
    return result;
}
