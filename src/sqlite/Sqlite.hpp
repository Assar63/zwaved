#pragma once

/// Shared SQLite plumbing for the daemon's persistent stores.
///
/// Seven stores (node-registry, node-metadata, node-values, pending-queue,
/// policy-register, scene-store, span-store) each open their own connection
/// to the same `nodes.db`. They used to carry a private copy of the same
/// forty lines — an open-and-log sequence plus an RAII `sqlite3_stmt*`
/// wrapper — which is how six of them ended up without a busy timeout and
/// two of them without a checked write (#233, #234).
///
/// `Db` centralises the open path, including the pragmas every connection to
/// a shared file needs; `Stmt` centralises prepare / bind / step. Both log
/// under the owning module's `[Tag]` so operator-facing output is unchanged.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace Sqlite
{
/// How long a writer waits for a competing transaction before giving up.
/// Without this SQLite's default is zero — a collision returns SQLITE_BUSY
/// immediately and, since nothing in the daemon retries, the write is lost.
constexpr int BUSY_TIMEOUT_MS = 5000;

class Db;

/// RAII wrapper around `sqlite3_stmt*`. Prepares on construction (logging on
/// failure), finalizes on destruction. Bind methods chain. `valid()` reports
/// whether prepare succeeded — callers must check before stepping.
///
/// Obtained from `Db::prepare()`; not constructed directly.
class Stmt
{
  public:
    ~Stmt();

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

    auto bindText(int pos, const std::string& value) -> Stmt&;
    auto bindInt(int pos, int value) -> Stmt&;
    auto bindInt64(int pos, std::int64_t value) -> Stmt&;
    auto bindBlob(int pos, const void* data, int size) -> Stmt&;
    auto bindBlob(int pos, const std::vector<std::uint8_t>& value) -> Stmt&;

    /// Step once, returning the raw SQLite code (SQLITE_ROW / SQLITE_DONE / …).
    /// For row-producing statements; writes should use `execDone()`.
    [[nodiscard]] auto step() const -> int;

    /// Execute a statement expected to terminate with SQLITE_DONE, logging
    /// the SQLite error message if it doesn't. Every write goes through this
    /// so a failed write can never pass silently (#234).
    auto execDone() const -> void;

    [[nodiscard]] auto columnText(int col) const -> std::string;
    [[nodiscard]] auto columnInt(int col) const -> int;
    [[nodiscard]] auto columnInt64(int col) const -> std::int64_t;
    [[nodiscard]] auto columnBlob(int col) const -> std::vector<std::uint8_t>;

  private:
    friend class Db;

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): private; Db::prepare supplies each role
    Stmt(sqlite3* database, const char* sql, const char* tag, const char* label);

    sqlite3_stmt* stmt_ = nullptr;
    sqlite3* database_  = nullptr;
    const char* tag_    = nullptr;
    const char* label_  = nullptr;
};

/// An owning connection to one SQLite file, opened with the pragmas a
/// process-wide shared database needs (see `open()` for the list).
///
/// Move-only. A default-constructed `Db` is a valid but closed handle whose
/// `valid()` is false and whose `prepare()` yields an invalid `Stmt` — the
/// stores' "persistence is best-effort, fall back to in-memory" path.
class Db
{
  public:
    Db() = default;

    /// Open `path`, apply the standard pragmas, and log failures under
    /// `[tag]`. On failure the handle is left closed (`valid() == false`);
    /// `tag` must outlive the Db (a string literal at every call site).
    Db(const std::filesystem::path& path, const char* tag);

    ~Db();

    Db(const Db&)                    = delete;
    auto operator=(const Db&) -> Db& = delete;
    Db(Db&& other) noexcept;
    auto operator=(Db&& other) noexcept -> Db&;

    [[nodiscard]] auto valid() const -> bool
    {
        return db_ != nullptr;
    }

    /// The underlying handle, for the few call sites that still reach for
    /// the C API directly (schema migration, `sqlite3_column_*`).
    [[nodiscard]] auto raw() const -> sqlite3*
    {
        return db_;
    }

    [[nodiscard]] auto tag() const -> const char*
    {
        return tag_;
    }

    /// Prepare `sql`; `label` names the statement in any log line.
    [[nodiscard]] auto prepare(const char* sql, const char* label) const -> Stmt;

    /// Run a statement with no results (schema DDL). Returns false and logs
    /// on failure.
    auto exec(const char* sql, const char* label) const -> bool;

    /// Close the connection, leaving the handle default-constructed.
    auto close() -> void;

  private:
    sqlite3* db_     = nullptr;
    const char* tag_ = "sqlite";
};
}  // namespace Sqlite
