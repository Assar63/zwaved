#include "MessageBus.hpp"
#include "PendingQueue.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// Bus-driven test for WakeUpOrchestrator (#68). No live dongle: we
// seed the persistent pending queue, publish a WakeUpNotification, and
// assert on the command sequence the orchestrator emits — each queued
// payload as a SendDataCommand, then a single
// SendWakeUpNoMoreInformationCommand, then a WakeUpCycleComplete.
//
// The orchestrator wires its subscription via __attribute__((constructor))
// at load, so simply linking WakeUpOrchestrator.cpp arms it. It uses the
// PendingQueue::instance() singleton, which resolves its DB path from a
// StorageConfig bus event — we point that at a per-process tmp file in
// SetUpTestSuite before the singleton first opens.

namespace
{
using MessageBus::SubscriptionGuard;

// Any 4 bytes work; binds the queue to a network so enqueue/drain are
// in-scope.
const std::vector<std::uint8_t> kHomeId{0xE2, 0xA1, 0xB0, 0x7C};

auto payload(std::initializer_list<std::uint8_t> bytes) -> std::vector<std::uint8_t>
{
    return {bytes};
}

// A single ordered log across the three command/event types so the test
// can assert relative ordering (all SendData before the sleep nudge,
// the cycle-complete last).
struct Recorder
{
    std::vector<std::string> log;
    std::vector<SubscriptionGuard> guards;

    Recorder()
    {
        guards.emplace_back(MessageBus::subscribe<MessageBus::SendDataCommand>(
            [this](const MessageBus::SendDataCommand& cmd)
            {
                std::string entry = "send:" + std::to_string(cmd.nodeId) + ":";
                for (const auto byte : cmd.payload)
                {
                    entry += std::to_string(byte) + ",";
                }
                log.push_back(entry);
            }));
        guards.emplace_back(MessageBus::subscribe<MessageBus::SendWakeUpNoMoreInformationCommand>(
            [this](const MessageBus::SendWakeUpNoMoreInformationCommand& cmd)
            { log.push_back("nomore:" + std::to_string(cmd.nodeId)); }));
        guards.emplace_back(MessageBus::subscribe<MessageBus::WakeUpCycleComplete>(
            [this](const MessageBus::WakeUpCycleComplete& evt)
            { log.push_back("complete:" + std::to_string(evt.nodeId) + ":" + std::to_string(evt.drainedCount)); }));
    }
};

class WakeUpOrchestratorTest : public ::testing::Test
{
  protected:
    static std::filesystem::path dbPath_;

    static void SetUpTestSuite()
    {
        const auto unique = std::to_string(::getpid()) + "-" + std::to_string(std::random_device{}());
        dbPath_           = std::filesystem::temp_directory_path() / ("zwaved-wakeup-test-" + unique + ".db");
        // Point the PendingQueue singleton at our tmp dir *before* its
        // first use, then bind the home so enqueue/drain are in scope.
        MessageBus::publish(MessageBus::StorageConfig{.stateDir = dbPath_.parent_path().string()});
        PendingQueue::instance().setHomeId(kHomeId);
    }

    static void TearDownTestSuite()
    {
        std::error_code errorCode;
        std::filesystem::remove(dbPath_, errorCode);
    }
};

std::filesystem::path WakeUpOrchestratorTest::dbPath_;
}  // namespace

TEST_F(WakeUpOrchestratorTest, DrainsQueuedCommandsThenSendsBackToSleep)
{
    constexpr std::uint8_t node = 9;
    PendingQueue::instance().enqueue(node, payload({0x25, 0x01, 0xFF}));
    PendingQueue::instance().enqueue(node, payload({0x80, 0x02}));

    Recorder rec;
    MessageBus::publish(MessageBus::WakeUpNotification{.sourceNodeId = node});

    ASSERT_EQ(rec.log.size(), 4U);
    EXPECT_EQ(rec.log[0], "send:9:37,1,255,");  // 0x25,0x01,0xFF in order
    EXPECT_EQ(rec.log[1], "send:9:128,2,");     // 0x80,0x02
    EXPECT_EQ(rec.log[2], "nomore:9");          // sleep nudge after all sends
    EXPECT_EQ(rec.log[3], "complete:9:2");      // cycle complete, drained 2
}

TEST_F(WakeUpOrchestratorTest, EmptyQueueStillSendsBackToSleep)
{
    constexpr std::uint8_t node = 11;  // nothing enqueued for this node

    Recorder rec;
    MessageBus::publish(MessageBus::WakeUpNotification{.sourceNodeId = node});

    // No SendData; the node still gets told to sleep, and the cycle
    // reports a drained count of 0.
    ASSERT_EQ(rec.log.size(), 2U);
    EXPECT_EQ(rec.log[0], "nomore:11");
    EXPECT_EQ(rec.log[1], "complete:11:0");
}

TEST_F(WakeUpOrchestratorTest, HigherPriorityDrainsFirst)
{
    constexpr std::uint8_t node = 13;
    PendingQueue::instance().enqueue(node, payload({0xA1}), PendingQueue::PRIORITY_NORMAL);
    PendingQueue::instance().enqueue(node, payload({0xB2}), PendingQueue::PRIORITY_HIGH);

    Recorder rec;
    MessageBus::publish(MessageBus::WakeUpNotification{.sourceNodeId = node});

    ASSERT_EQ(rec.log.size(), 4U);
    EXPECT_EQ(rec.log[0], "send:13:178,");  // 0xB2 HIGH first
    EXPECT_EQ(rec.log[1], "send:13:161,");  // 0xA1 NORMAL second
    EXPECT_EQ(rec.log[2], "nomore:13");
    EXPECT_EQ(rec.log[3], "complete:13:2");
}
