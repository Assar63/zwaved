#include "PendingQueue.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// Aeotec Z-Stick Gen5 home ID lifted from a real network — any
// 4 bytes work; this just keeps the formatted hex deterministic.
const std::vector<std::uint8_t> kHomeId{0xE2, 0xA1, 0xB0, 0x7C};

/// Per-test fixture that gives each TEST() its own DB file in
/// the system tmpdir. Cleans up on destruction so the test
/// suite doesn't leak files.
class PendingQueueTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const auto unique = std::to_string(::getpid()) + "-" + std::to_string(std::random_device{}());
        dbPath_           = std::filesystem::temp_directory_path() / ("zwaved-pendingqueue-test-" + unique + ".db");
    }
    void TearDown() override
    {
        std::error_code errorCode;
        std::filesystem::remove(dbPath_, errorCode);
    }
    std::filesystem::path dbPath_;
};

auto payload(std::initializer_list<std::uint8_t> bytes) -> std::vector<std::uint8_t>
{
    return {bytes};
}
}  // namespace

TEST_F(PendingQueueTest, EnqueueAndDrainReturnsInOrder)
{
    PendingQueue::Queue queue(dbPath_);
    queue.setHomeId(kHomeId);
    queue.enqueue(5, payload({0x25, 0x01, 0xFF}));
    queue.enqueue(5, payload({0x80, 0x02}));

    const auto drained = queue.drain(5);
    ASSERT_EQ(drained.size(), 2);
    EXPECT_EQ(drained[0], payload({0x25, 0x01, 0xFF}));
    EXPECT_EQ(drained[1], payload({0x80, 0x02}));
}

TEST_F(PendingQueueTest, DrainPopsAndLeavesEmpty)
{
    PendingQueue::Queue queue(dbPath_);
    queue.setHomeId(kHomeId);
    queue.enqueue(5, payload({0x25, 0x01, 0xFF}));
    EXPECT_EQ(queue.drain(5).size(), 1);
    // Second drain on the same node sees nothing.
    EXPECT_TRUE(queue.drain(5).empty());
}

TEST_F(PendingQueueTest, DrainScopedToNode)
{
    PendingQueue::Queue queue(dbPath_);
    queue.setHomeId(kHomeId);
    queue.enqueue(5, payload({0x25, 0x01, 0xFF}));
    queue.enqueue(7, payload({0x80, 0x02}));

    EXPECT_EQ(queue.drain(5).size(), 1);
    // Draining node 5 must not touch node 7's entries.
    const auto seven = queue.drain(7);
    ASSERT_EQ(seven.size(), 1);
    EXPECT_EQ(seven[0], payload({0x80, 0x02}));
}

TEST_F(PendingQueueTest, HigherPriorityDrainsFirst)
{
    PendingQueue::Queue queue(dbPath_);
    queue.setHomeId(kHomeId);
    // Enqueue NORMAL first, then HIGH — HIGH (lower priority
    // number) should come out first regardless of enqueue order.
    queue.enqueue(5, payload({0xA1}), PendingQueue::PRIORITY_NORMAL);
    queue.enqueue(5, payload({0xB2}), PendingQueue::PRIORITY_HIGH);
    queue.enqueue(5, payload({0xC3}), PendingQueue::PRIORITY_LOW);

    const auto drained = queue.drain(5);
    ASSERT_EQ(drained.size(), 3);
    EXPECT_EQ(drained[0], payload({0xB2}));  // HIGH first
    EXPECT_EQ(drained[1], payload({0xA1}));  // NORMAL second
    EXPECT_EQ(drained[2], payload({0xC3}));  // LOW last
}

TEST_F(PendingQueueTest, FifoWithinPriority)
{
    PendingQueue::Queue queue(dbPath_);
    queue.setHomeId(kHomeId);
    // Three entries at the same priority — order preserved by
    // the sequence (rowid) tiebreaker in the ORDER BY clause.
    queue.enqueue(5, payload({0x01}));
    queue.enqueue(5, payload({0x02}));
    queue.enqueue(5, payload({0x03}));

    const auto drained = queue.drain(5);
    ASSERT_EQ(drained.size(), 3);
    EXPECT_EQ(drained[0], payload({0x01}));
    EXPECT_EQ(drained[1], payload({0x02}));
    EXPECT_EQ(drained[2], payload({0x03}));
}

TEST_F(PendingQueueTest, ClearForNodeRemovesAllEntries)
{
    PendingQueue::Queue queue(dbPath_);
    queue.setHomeId(kHomeId);
    queue.enqueue(5, payload({0x01}));
    queue.enqueue(5, payload({0x02}), PendingQueue::PRIORITY_HIGH);
    queue.enqueue(7, payload({0x03}));

    queue.clearForNode(5);
    EXPECT_TRUE(queue.drain(5).empty());
    // Node 7 must NOT be affected.
    EXPECT_EQ(queue.drain(7).size(), 1);
}

TEST_F(PendingQueueTest, PersistAcrossRestart)
{
    // The whole point of SQLite-backing the queue. Construct one
    // Queue, enqueue, destroy it (which closes the connection).
    // Construct a fresh Queue against the same path — entries
    // must survive.
    {
        PendingQueue::Queue first(dbPath_);
        first.setHomeId(kHomeId);
        first.enqueue(5, payload({0x25, 0x01, 0xFF}), PendingQueue::PRIORITY_HIGH);
        first.enqueue(5, payload({0x80, 0x02}));
    }  // first's destructor closes the SQLite connection

    PendingQueue::Queue second(dbPath_);
    second.setHomeId(kHomeId);
    const auto drained = second.drain(5);
    ASSERT_EQ(drained.size(), 2);
    EXPECT_EQ(drained[0], payload({0x25, 0x01, 0xFF}));  // HIGH first
    EXPECT_EQ(drained[1], payload({0x80, 0x02}));
}

TEST_F(PendingQueueTest, DifferentHomeIdIsolatesQueueView)
{
    const std::vector<std::uint8_t> otherHome{0x12, 0x34, 0x56, 0x78};

    PendingQueue::Queue queue(dbPath_);
    queue.setHomeId(kHomeId);
    queue.enqueue(5, payload({0xAA}));

    // Re-bind to a different home (different network). The
    // previous home's entry stays in the DB but is invisible
    // under the new binding.
    queue.setHomeId(otherHome);
    EXPECT_TRUE(queue.drain(5).empty());

    // Re-bind back — the original entry is still there.
    queue.setHomeId(kHomeId);
    const auto drained = queue.drain(5);
    ASSERT_EQ(drained.size(), 1);
    EXPECT_EQ(drained[0], payload({0xAA}));
}

TEST_F(PendingQueueTest, PeekDoesNotPop)
{
    PendingQueue::Queue queue(dbPath_);
    queue.setHomeId(kHomeId);
    queue.enqueue(5, payload({0xAA}), PendingQueue::PRIORITY_HIGH);
    queue.enqueue(5, payload({0xBB}));

    const auto snapshot = queue.peek(5);
    ASSERT_EQ(snapshot.size(), 2);
    EXPECT_EQ(snapshot[0].priority, PendingQueue::PRIORITY_HIGH);
    EXPECT_EQ(snapshot[0].payload, payload({0xAA}));
    EXPECT_EQ(snapshot[1].priority, PendingQueue::PRIORITY_NORMAL);

    // Peek must leave the queue intact.
    const auto drained = queue.drain(5);
    EXPECT_EQ(drained.size(), 2);
}

TEST_F(PendingQueueTest, EnqueueWithNoHomeIdIsDropped)
{
    PendingQueue::Queue queue(dbPath_);
    // No setHomeId call — enqueue should warn and drop, not crash.
    queue.enqueue(5, payload({0xAA}));
    queue.setHomeId(kHomeId);
    EXPECT_TRUE(queue.drain(5).empty());
}
