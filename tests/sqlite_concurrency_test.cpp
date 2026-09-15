// Shared-nodes.db concurrency (#233, #234).
//
// The daemon opens seven SQLite connections to one `nodes.db` from three
// threads: the protocol thread writes (registry upserts, the value recorder,
// the orchestrators) while the external-API thread reads for every `Get*`
// D-Bus method. Every existing store test is single-threaded against its own
// connection, which is exactly why the missing busy timeout went unnoticed —
// with SQLite's default zero timeout a colliding writer gets SQLITE_BUSY back
// immediately and, since nothing retries, the write is silently lost.
//
// These tests are the concurrency analogue of the existing "daemon stopped,
// daemon started again" two-instance tests: several connections to one file,
// writing at the same time, asserting every write lands.

#include "NodeMetadata.hpp"
#include "NodeValues.hpp"
#include "Sqlite.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const std::vector<std::uint8_t> HOME{0xDE, 0xAD, 0xBE, 0xEF};
constexpr std::uint8_t NODE = 7;

/// Enough writers to make a collision near-certain, and enough rows each that
/// the run spans many transactions rather than one lucky uncontended window.
constexpr int WRITERS         = 4;
constexpr int ROWS_PER_WRITER = 60;

auto tempDb(const char* name) -> std::filesystem::path
{
    const auto path = std::filesystem::temp_directory_path() / name;
    // WAL leaves -wal / -shm siblings; clear them so each run starts fresh.
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
    return path;
}
}  // namespace

// Every connection must come up with the pragmas a shared file needs.
TEST(SqliteConnection, AppliesWalAndBusyTimeout)
{
    const auto path = tempDb("zwaved_sqlite_pragmas.db");
    const Sqlite::Db db(path, "PragmaTest");
    ASSERT_TRUE(db.valid());

    const auto journalMode = db.prepare("PRAGMA journal_mode", "read journal_mode");
    ASSERT_TRUE(journalMode.valid());
    ASSERT_EQ(journalMode.step(), SQLITE_ROW);
    EXPECT_EQ(journalMode.columnText(0), "wal");

    const auto sync = db.prepare("PRAGMA synchronous", "read synchronous");
    ASSERT_TRUE(sync.valid());
    ASSERT_EQ(sync.step(), SQLITE_ROW);
    EXPECT_EQ(sync.columnInt(0), 1);  // NORMAL

    const auto foreignKeys = db.prepare("PRAGMA foreign_keys", "read foreign_keys");
    ASSERT_TRUE(foreignKeys.valid());
    ASSERT_EQ(foreignKeys.step(), SQLITE_ROW);
    EXPECT_EQ(foreignKeys.columnInt(0), 1);
}

// A closed Db is usable — it just yields invalid statements, which is the
// stores' "persistence is best-effort, fall back to in-memory" path.
TEST(SqliteConnection, ClosedHandleYieldsInvalidStatements)
{
    const Sqlite::Db db;
    EXPECT_FALSE(db.valid());
    EXPECT_FALSE(db.prepare("SELECT 1", "select").valid());
    EXPECT_FALSE(db.exec("SELECT 1", "select"));
}

// Several NodeValues connections to one file, writing at once. Before the
// busy timeout this dropped writes; every row must now be present.
TEST(SqliteConcurrency, ConcurrentWritersOnOneFileAllLand)
{
    const auto path = tempDb("zwaved_sqlite_concurrent_values.db");

    std::vector<std::thread> writers;
    writers.reserve(WRITERS);
    for (int writer = 0; writer < WRITERS; ++writer)
    {
        writers.emplace_back(
            [path, writer]
            {
                // One Store per thread == one connection per thread, the
                // daemon's actual shape.
                NodeValues::Store store(path);
                store.setHomeId(HOME);
                for (int row = 0; row < ROWS_PER_WRITER; ++row)
                {
                    store.record(NODE, "w" + std::to_string(writer) + ":" + std::to_string(row), "v");
                }
            });
    }
    for (auto& thread : writers)
    {
        thread.join();
    }

    NodeValues::Store reader(path);
    reader.setHomeId(HOME);
    EXPECT_EQ(reader.getAll(NODE).size(), static_cast<std::size_t>(WRITERS * ROWS_PER_WRITER));
}

// The cross-store case: different modules, different tables, one file. This is
// what the daemon does every time a report arrives while a D-Bus client reads.
TEST(SqliteConcurrency, WritesFromDifferentStoresToOneFileAllLand)
{
    const auto path    = tempDb("zwaved_sqlite_concurrent_stores.db");
    constexpr int ROWS = 80;

    std::thread values(
        [path]
        {
            NodeValues::Store store(path);
            store.setHomeId(HOME);
            for (int row = 0; row < ROWS; ++row)
            {
                store.record(NODE, "value:" + std::to_string(row), "v");
            }
        });
    std::thread metadata(
        [path]
        {
            NodeMetadata::Store store(path);
            store.setHomeId(HOME);
            for (int row = 0; row < ROWS; ++row)
            {
                store.set(NODE, "key:" + std::to_string(row), "v");
            }
        });
    values.join();
    metadata.join();

    NodeValues::Store valueReader(path);
    valueReader.setHomeId(HOME);
    EXPECT_EQ(valueReader.getAll(NODE).size(), static_cast<std::size_t>(ROWS));

    NodeMetadata::Store metadataReader(path);
    metadataReader.setHomeId(HOME);
    EXPECT_EQ(metadataReader.getAll(NODE).size(), static_cast<std::size_t>(ROWS));
}

// #234: NodeValues is reached from the bus dispatch thread (the recorder) and
// the external-API thread (GetNodeValues) through the *same* Store instance —
// one sqlite3 handle, no serialisation before the mutex landed. Readers must
// not tear or crash while a writer runs.
TEST(SqliteConcurrency, SharedStoreInstanceSurvivesConcurrentReadAndWrite)
{
    const auto path = tempDb("zwaved_sqlite_shared_instance.db");
    NodeValues::Store store(path);
    store.setHomeId(HOME);

    std::atomic<bool> writing{true};
    std::thread writer(
        [&store, &writing]
        {
            for (int row = 0; row < ROWS_PER_WRITER * WRITERS; ++row)
            {
                store.record(NODE, "shared:" + std::to_string(row), "v");
            }
            writing = false;
        });

    std::size_t reads = 0;
    while (writing)
    {
        // Never smaller than a previous read: record() only ever adds rows.
        const auto seen = store.getAll(NODE).size();
        EXPECT_GE(seen, reads);
        reads = seen;
    }
    writer.join();

    EXPECT_EQ(store.getAll(NODE).size(), static_cast<std::size_t>(ROWS_PER_WRITER * WRITERS));
}
