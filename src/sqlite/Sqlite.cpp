#include "Sqlite.hpp"

#include "../logger/Logger.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <sqlite3.h>

namespace
{
/// Pragmas applied to every connection the daemon opens.
///
/// * `journal_mode=WAL` — one writer concurrent with many readers, which is
///   exactly this daemon's shape: the protocol thread writes while the
///   external-API thread reads. Unlike the default rollback journal, a
///   reader never blocks the writer. WAL is a property of the *file*, so
///   whichever connection opens first sets it for all of them; it is
///   recorded in the database header and survives restarts.
/// * `synchronous=NORMAL` — safe under WAL (a crash can lose the tail of the
///   last transaction but cannot corrupt the database), and avoids an fsync
///   on every sensor report on an embedded target's flash.
/// * `foreign_keys=ON` — off by default in SQLite; harmless today and
///   correct the moment any table grows an FK.
///
/// A pragma failing is a warning, not an error: WAL is unavailable on some
/// network filesystems, and a store that falls back to the rollback journal
/// still works (it just serialises more).
struct Pragma
{
    const char* sql;
    const char* label;
};

constexpr std::array<Pragma, 3> PRAGMAS{{
    {"PRAGMA journal_mode=WAL", "journal_mode=WAL"},
    {"PRAGMA synchronous=NORMAL", "synchronous=NORMAL"},
    {"PRAGMA foreign_keys=ON", "foreign_keys=ON"},
}};
}  // namespace

// ---- Stmt ------------------------------------------------------------

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): private; Db::prepare supplies each role
Sqlite::Stmt::Stmt(sqlite3* database, const char* sql, const char* tag, const char* label)
    : database_(database),
      tag_(tag),
      label_(label)
{
    if (database == nullptr)
    {
        return;  // closed Db — yields an invalid Stmt, as documented
    }
    if (sqlite3_prepare_v2(database, sql, -1, &stmt_, nullptr) != SQLITE_OK)
    {
        Logger::error(std::string("[") + tag_ + "] prepare " + label_ + " failed: " + sqlite3_errmsg(database));
        stmt_ = nullptr;
    }
}

Sqlite::Stmt::~Stmt()
{
    sqlite3_finalize(stmt_);  // a no-op on nullptr
}

auto Sqlite::Stmt::bindText(int pos, const std::string& value) -> Stmt&
{
    sqlite3_bind_text(stmt_, pos, value.c_str(), -1, SQLITE_TRANSIENT);
    return *this;
}

auto Sqlite::Stmt::bindInt(int pos, int value) -> Stmt&
{
    sqlite3_bind_int(stmt_, pos, value);
    return *this;
}

auto Sqlite::Stmt::bindInt64(int pos, std::int64_t value) -> Stmt&
{
    sqlite3_bind_int64(stmt_, pos, value);
    return *this;
}

auto Sqlite::Stmt::bindBlob(int pos, const void* data, int size) -> Stmt&
{
    sqlite3_bind_blob(stmt_, pos, data, size, SQLITE_TRANSIENT);
    return *this;
}

auto Sqlite::Stmt::bindBlob(int pos, const std::vector<std::uint8_t>& value) -> Stmt&
{
    return bindBlob(pos, value.data(), static_cast<int>(value.size()));
}

auto Sqlite::Stmt::step() const -> int
{
    return sqlite3_step(stmt_);
}

auto Sqlite::Stmt::execDone() const -> void
{
    if (sqlite3_step(stmt_) != SQLITE_DONE)
    {
        Logger::error(std::string("[") + tag_ + "] " + label_ + " failed: " + sqlite3_errmsg(database_));
    }
}

auto Sqlite::Stmt::columnText(int col) const -> std::string
{
    // sqlite3_column_text returns `const unsigned char*`; build the string
    // from the byte range to avoid a reinterpret_cast.
    const auto* text = sqlite3_column_text(stmt_, col);
    if (text == nullptr)
    {
        return {};
    }
    const int len = sqlite3_column_bytes(stmt_, col);
    return {text, text + len};
}

auto Sqlite::Stmt::columnInt(int col) const -> int
{
    return sqlite3_column_int(stmt_, col);
}

auto Sqlite::Stmt::columnInt64(int col) const -> std::int64_t
{
    return sqlite3_column_int64(stmt_, col);
}

auto Sqlite::Stmt::columnBlob(int col) const -> std::vector<std::uint8_t>
{
    const auto* data = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt_, col));
    const int size   = sqlite3_column_bytes(stmt_, col);
    if (data == nullptr || size <= 0)
    {
        return {};
    }
    return {data, data + size};
}

// ---- Db --------------------------------------------------------------

Sqlite::Db::Db(const std::filesystem::path& path, const char* tag)
    : tag_(tag)
{
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
    {
        Logger::error(std::string("[") + tag_ + "] cannot open " + path.string() + ": " + sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }

    // Six connections share nodes.db across three threads; without a busy
    // handler a collision drops the write on the floor (#233).
    if (sqlite3_busy_timeout(db_, BUSY_TIMEOUT_MS) != SQLITE_OK)
    {
        Logger::warn(std::string("[") + tag_ + "] cannot set busy timeout: " + sqlite3_errmsg(db_));
    }

    for (const auto& pragma : PRAGMAS)
    {
        char* err = nullptr;
        if (sqlite3_exec(db_, pragma.sql, nullptr, nullptr, &err) != SQLITE_OK)
        {
            Logger::warn(std::string("[") + tag_ + "] " + pragma.label + " failed: " + (err != nullptr ? err : "?"));
        }
        sqlite3_free(err);
    }
}

Sqlite::Db::~Db()
{
    sqlite3_close(db_);  // a no-op on nullptr
}

Sqlite::Db::Db(Db&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)),
      tag_(other.tag_)
{
}

auto Sqlite::Db::operator=(Db&& other) noexcept -> Db&
{
    if (this != &other)
    {
        sqlite3_close(db_);
        db_  = std::exchange(other.db_, nullptr);
        tag_ = other.tag_;
    }
    return *this;
}

auto Sqlite::Db::prepare(const char* sql, const char* label) const -> Stmt
{
    // Guaranteed copy elision (C++17): the prvalue initialises the caller's
    // object directly, so Stmt needs no move constructor.
    return Stmt{db_, sql, tag_, label};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SQL text vs log label are distinct at call sites
auto Sqlite::Db::exec(const char* sql, const char* label) const -> bool
{
    if (db_ == nullptr)
    {
        return false;
    }
    char* err       = nullptr;
    const bool okay = sqlite3_exec(db_, sql, nullptr, nullptr, &err) == SQLITE_OK;
    if (!okay)
    {
        Logger::error(std::string("[") + tag_ + "] " + label + " failed: " + (err != nullptr ? err : "?"));
    }
    sqlite3_free(err);
    return okay;
}

auto Sqlite::Db::close() -> void
{
    sqlite3_close(db_);
    db_ = nullptr;
}
