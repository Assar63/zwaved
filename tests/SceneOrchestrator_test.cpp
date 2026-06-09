#include "MessageBus.hpp"
#include "SceneStore.hpp"

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

// Bus-driven test for SceneOrchestrator (#121). No live dongle: we seed the
// scene store (a scene + a trigger), publish a CentralSceneNotification, and
// assert on the SendDataCommands + SceneActivated the orchestrator emits.
//
// The orchestrator arms its subscription via __attribute__((constructor)) at
// load, so linking SceneOrchestrator.cpp is enough. It uses
// SceneStore::instance(), whose DB path resolves from a StorageConfig bus
// event — we point that at a per-process tmp file in SetUpTestSuite before
// the singleton first opens.

namespace
{
using MessageBus::SubscriptionGuard;

const std::vector<std::uint8_t> kHomeId{0xE2, 0xA1, 0xB0, 0x7C};

auto payload(std::initializer_list<std::uint8_t> bytes) -> std::vector<std::uint8_t>
{
    return {bytes};
}

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
        guards.emplace_back(MessageBus::subscribe<MessageBus::SceneActivated>(
            [this](const MessageBus::SceneActivated& evt)
            {
                log.push_back("scene:" + std::to_string(evt.sourceNodeId) + ":" + evt.sceneId + ":" +
                              std::to_string(evt.actionCount));
            }));
    }
};

class SceneOrchestratorTest : public ::testing::Test
{
  protected:
    static std::filesystem::path dbPath_;

    static void SetUpTestSuite()
    {
        const auto unique = std::to_string(::getpid()) + "-" + std::to_string(std::random_device{}());
        dbPath_           = std::filesystem::temp_directory_path() / ("zwaved-sceneorch-test-" + unique + ".db");
        MessageBus::publish(MessageBus::StorageConfig{.stateDir = dbPath_.parent_path().string()});
        SceneStore::instance().setHomeId(kHomeId);
    }

    static void TearDownTestSuite()
    {
        std::error_code errorCode;
        std::filesystem::remove(dbPath_, errorCode);
    }
};

std::filesystem::path SceneOrchestratorTest::dbPath_;
}  // namespace

TEST_F(SceneOrchestratorTest, RunsBoundSceneActionsInOrder)
{
    // Scene "tv" turns node 5 on and node 6 off; bound to node 7 / scene 1 / press 1x.
    SceneStore::instance().setScene("tv",
                                    {SceneStore::Action{.targetNodeId = 5, .ccPayload = payload({0x25, 0x01, 0xFF})},
                                     SceneStore::Action{.targetNodeId = 6, .ccPayload = payload({0x25, 0x01, 0x00})}});
    SceneStore::instance().bindTrigger(SceneStore::SOURCE_CENTRAL_SCENE, 7, 1, 0, "tv");

    Recorder rec;
    MessageBus::publish(MessageBus::CentralSceneNotification{
        .sourceNodeId = 7, .sequenceNumber = 1, .keyAttribute = 0, .sceneNumber = 1, .slowRefresh = false});

    ASSERT_EQ(rec.log.size(), 3U);
    EXPECT_EQ(rec.log[0], "send:5:37,1,255,");
    EXPECT_EQ(rec.log[1], "send:6:37,1,0,");
    EXPECT_EQ(rec.log[2], "scene:7:tv:2");
}

TEST_F(SceneOrchestratorTest, ContextDependentBySourceNode)
{
    // Same scene number (1) from two different senders runs different scenes.
    SceneStore::instance().setScene("living",
                                    {SceneStore::Action{.targetNodeId = 5, .ccPayload = payload({0x25, 0x01, 0xFF})}});
    SceneStore::instance().setScene("hall",
                                    {SceneStore::Action{.targetNodeId = 8, .ccPayload = payload({0x25, 0x01, 0x00})}});
    SceneStore::instance().bindTrigger(SceneStore::SOURCE_CENTRAL_SCENE, 20, 1, 0, "living");
    SceneStore::instance().bindTrigger(SceneStore::SOURCE_CENTRAL_SCENE, 21, 1, 0, "hall");

    Recorder rec;
    MessageBus::publish(MessageBus::CentralSceneNotification{
        .sourceNodeId = 21, .sequenceNumber = 9, .keyAttribute = 0, .sceneNumber = 1, .slowRefresh = false});

    ASSERT_EQ(rec.log.size(), 2U);
    EXPECT_EQ(rec.log[0], "send:8:37,1,0,");  // hall scene, not living
    EXPECT_EQ(rec.log[1], "scene:21:hall:1");
}

TEST_F(SceneOrchestratorTest, UnboundPressIsInert)
{
    Recorder rec;
    MessageBus::publish(MessageBus::CentralSceneNotification{
        .sourceNodeId = 99, .sequenceNumber = 1, .keyAttribute = 0, .sceneNumber = 4, .slowRefresh = false});
    EXPECT_TRUE(rec.log.empty());  // no trigger → no SendData, no SceneActivated
}

TEST_F(SceneOrchestratorTest, TriggerToMissingSceneActivatesWithZeroActions)
{
    SceneStore::instance().bindTrigger(SceneStore::SOURCE_CENTRAL_SCENE, 30, 2, 3, "deleted-scene");  // never created
    Recorder rec;
    MessageBus::publish(MessageBus::CentralSceneNotification{
        .sourceNodeId = 30, .sequenceNumber = 1, .keyAttribute = 3, .sceneNumber = 2, .slowRefresh = false});
    ASSERT_EQ(rec.log.size(), 1U);
    EXPECT_EQ(rec.log[0], "scene:30:deleted-scene:0");  // activated, but no actions dispatched
}

TEST_F(SceneOrchestratorTest, RunsSceneFromBasicSet)
{
    // A Basic Set value 0xFF from node 40 runs scene "lights-on".
    SceneStore::instance().setScene("lights-on",
                                    {SceneStore::Action{.targetNodeId = 9, .ccPayload = payload({0x25, 0x01, 0xFF})}});
    SceneStore::instance().bindTrigger(SceneStore::SOURCE_BASIC_SET, 40, 0xFF, 0, "lights-on");

    Recorder rec;
    MessageBus::publish(MessageBus::BasicSetReceived{.sourceNodeId = 40, .value = 0xFF});

    ASSERT_EQ(rec.log.size(), 2U);
    EXPECT_EQ(rec.log[0], "send:9:37,1,255,");
    EXPECT_EQ(rec.log[1], "scene:40:lights-on:1");  // SceneActivated carries value as sceneNumber
}

TEST_F(SceneOrchestratorTest, BasicSetDistinctFromCentralScene)
{
    // Same (node, selector) on Basic Set vs Central Scene run different scenes.
    SceneStore::instance().setScene("basic-scene",
                                    {SceneStore::Action{.targetNodeId = 9, .ccPayload = payload({0x25, 0x01, 0x00})}});
    SceneStore::instance().bindTrigger(SceneStore::SOURCE_BASIC_SET, 41, 1, 0, "basic-scene");

    Recorder rec;
    // A Central Scene press with the same (node, selector, key) is unbound.
    MessageBus::publish(MessageBus::CentralSceneNotification{
        .sourceNodeId = 41, .sequenceNumber = 1, .keyAttribute = 0, .sceneNumber = 1, .slowRefresh = false});
    EXPECT_TRUE(rec.log.empty());  // Central Scene side has no binding

    MessageBus::publish(MessageBus::BasicSetReceived{.sourceNodeId = 41, .value = 1});
    ASSERT_EQ(rec.log.size(), 2U);
    EXPECT_EQ(rec.log[0], "send:9:37,1,0,");
    EXPECT_EQ(rec.log[1], "scene:41:basic-scene:1");
}

TEST_F(SceneOrchestratorTest, RunsSceneFromSceneActivation)
{
    // A Scene Activation Set for scene 3 from node 50 runs scene "movie".
    SceneStore::instance().setScene("movie",
                                    {SceneStore::Action{.targetNodeId = 9, .ccPayload = payload({0x25, 0x01, 0xFF})}});
    SceneStore::instance().bindTrigger(SceneStore::SOURCE_SCENE_ACTIVATION, 50, 3, 0, "movie");

    Recorder rec;
    MessageBus::publish(MessageBus::SceneActivationSet{.sourceNodeId = 50, .sceneId = 3, .dimmingDuration = 0});

    ASSERT_EQ(rec.log.size(), 2U);
    EXPECT_EQ(rec.log[0], "send:9:37,1,255,");
    EXPECT_EQ(rec.log[1], "scene:50:movie:1");  // SceneActivated carries sceneId as sceneNumber
}
