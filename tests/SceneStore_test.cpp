#include "SceneStore.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const std::vector<std::uint8_t> kHomeId{0xE2, 0xA1, 0xB0, 0x7C};
const std::vector<std::uint8_t> kOtherHome{0x11, 0x22, 0x33, 0x44};

// A SwitchBinary SET "on" to node 5 — representative scene action payload.
auto action(std::uint8_t target, std::initializer_list<std::uint8_t> payload) -> SceneStore::Action
{
    return SceneStore::Action{.targetNodeId = target, .ccPayload = std::vector<std::uint8_t>(payload)};
}

class SceneStoreTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const auto unique = std::to_string(::getpid()) + "-" + std::to_string(std::random_device{}());
        dbPath_           = std::filesystem::temp_directory_path() / ("zwaved-scenestore-test-" + unique + ".db");
    }
    void TearDown() override
    {
        std::error_code errorCode;
        std::filesystem::remove(dbPath_, errorCode);
    }
    std::filesystem::path dbPath_;
};
}  // namespace

TEST_F(SceneStoreTest, SceneRoundTrip)
{
    SceneStore::Store store(dbPath_);
    store.setHomeId(kHomeId);

    const std::vector<SceneStore::Action> actions{action(5, {0x25, 0x01, 0xFF}), action(6, {0x25, 0x01, 0x00})};
    store.setScene("TV mode", actions);

    const auto got = store.getScene("TV mode");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->size(), 2U);
    EXPECT_EQ((*got)[0].targetNodeId, 5);
    EXPECT_EQ((*got)[0].ccPayload, (std::vector<std::uint8_t>{0x25, 0x01, 0xFF}));
    EXPECT_EQ((*got)[1].targetNodeId, 6);
    EXPECT_EQ((*got)[1].ccPayload, (std::vector<std::uint8_t>{0x25, 0x01, 0x00}));
}

TEST_F(SceneStoreTest, GetMissingSceneIsNullopt)
{
    SceneStore::Store store(dbPath_);
    store.setHomeId(kHomeId);
    EXPECT_FALSE(store.getScene("nope").has_value());
}

TEST_F(SceneStoreTest, SetSceneReplaces)
{
    SceneStore::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.setScene("s", {action(1, {0x20, 0x01, 0x00})});
    store.setScene("s", {action(2, {0x20, 0x01, 0xFF}), action(3, {0x20, 0x01, 0xFF})});
    const auto got = store.getScene("s");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->size(), 2U);
    EXPECT_EQ((*got)[0].targetNodeId, 2);
}

TEST_F(SceneStoreTest, EmptySceneRoundTrips)
{
    SceneStore::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.setScene("empty", {});
    const auto got = store.getScene("empty");
    ASSERT_TRUE(got.has_value());
    EXPECT_TRUE(got->empty());
}

TEST_F(SceneStoreTest, ListAndDeleteScenes)
{
    SceneStore::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.setScene("bravo", {action(1, {0x01})});
    store.setScene("alpha", {action(2, {0x02})});
    EXPECT_EQ(store.listScenes(), (std::vector<std::string>{"alpha", "bravo"}));  // ascending
    store.deleteScene("alpha");
    EXPECT_EQ(store.listScenes(), (std::vector<std::string>{"bravo"}));
    EXPECT_FALSE(store.getScene("alpha").has_value());
}

TEST_F(SceneStoreTest, TriggerResolveHitAndMiss)
{
    SceneStore::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.bindTrigger(7, 1, 0, "TV mode");    // living room, scene 1, press 1x
    store.bindTrigger(12, 1, 0, "Good bye");  // hallway, same scene number

    EXPECT_EQ(store.resolveTrigger(7, 1, 0), std::optional<std::string>("TV mode"));
    EXPECT_EQ(store.resolveTrigger(12, 1, 0), std::optional<std::string>("Good bye"));
    EXPECT_FALSE(store.resolveTrigger(7, 2, 0).has_value());   // different scene number
    EXPECT_FALSE(store.resolveTrigger(99, 1, 0).has_value());  // unknown source
}

TEST_F(SceneStoreTest, TriggerRebindAndUnbind)
{
    SceneStore::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.bindTrigger(7, 1, 0, "scene-a");
    store.bindTrigger(7, 1, 0, "scene-b");  // rebind same key
    EXPECT_EQ(store.resolveTrigger(7, 1, 0), std::optional<std::string>("scene-b"));
    EXPECT_EQ(store.listTriggers().size(), 1U);  // rebind, not a second row
    store.unbindTrigger(7, 1, 0);
    EXPECT_FALSE(store.resolveTrigger(7, 1, 0).has_value());
    EXPECT_TRUE(store.listTriggers().empty());
}

TEST_F(SceneStoreTest, PersistsAcrossRestart)
{
    {
        SceneStore::Store store(dbPath_);
        store.setHomeId(kHomeId);
        store.setScene("TV mode", {action(5, {0x25, 0x01, 0xFF})});
        store.bindTrigger(7, 1, 0, "TV mode");
    }  // first "daemon" stops

    SceneStore::Store reopened(dbPath_);  // second "daemon" starts on the same file
    reopened.setHomeId(kHomeId);
    EXPECT_TRUE(reopened.getScene("TV mode").has_value());
    EXPECT_EQ(reopened.resolveTrigger(7, 1, 0), std::optional<std::string>("TV mode"));
}

TEST_F(SceneStoreTest, HomeScoping)
{
    SceneStore::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.setScene("TV mode", {action(5, {0x25, 0x01, 0xFF})});
    store.bindTrigger(7, 1, 0, "TV mode");

    store.setHomeId(kOtherHome);  // different network
    EXPECT_FALSE(store.getScene("TV mode").has_value());
    EXPECT_FALSE(store.resolveTrigger(7, 1, 0).has_value());
    EXPECT_TRUE(store.listScenes().empty());

    store.setHomeId(kHomeId);  // back to the original network
    EXPECT_TRUE(store.getScene("TV mode").has_value());
    EXPECT_EQ(store.resolveTrigger(7, 1, 0), std::optional<std::string>("TV mode"));
}

TEST_F(SceneStoreTest, NoHomeBoundIsGraceful)
{
    SceneStore::Store store(dbPath_);  // no setHomeId
    store.setScene("x", {action(1, {0x01})});
    EXPECT_FALSE(store.getScene("x").has_value());
    EXPECT_TRUE(store.listScenes().empty());
    EXPECT_FALSE(store.resolveTrigger(1, 1, 0).has_value());
}
