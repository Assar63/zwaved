#include "NodeMetadata.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const std::vector<std::uint8_t> kHomeId{0xE2, 0xA1, 0xB0, 0x7C};

class NodeMetadataTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const auto unique = std::to_string(::getpid()) + "-" + std::to_string(std::random_device{}());
        dbPath_           = std::filesystem::temp_directory_path() / ("zwaved-metadata-test-" + unique + ".db");
    }
    void TearDown() override
    {
        std::error_code errorCode;
        std::filesystem::remove(dbPath_, errorCode);
    }
    std::filesystem::path dbPath_;
};
}  // namespace

TEST_F(NodeMetadataTest, SetGetRoundTrips)
{
    NodeMetadata::Store store(dbPath_);
    store.setHomeId(kHomeId);

    EXPECT_FALSE(store.get(5, "name").has_value());
    store.set(5, "name", "Kitchen light");
    store.set(5, "room", "Kitchen");

    ASSERT_TRUE(store.get(5, "name").has_value());
    EXPECT_EQ(*store.get(5, "name"), "Kitchen light");
    EXPECT_EQ(*store.get(5, "room"), "Kitchen");
}

TEST_F(NodeMetadataTest, SetUpdatesExistingKey)
{
    NodeMetadata::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.set(5, "name", "old");
    store.set(5, "name", "new");

    const auto all = store.getAll(5);
    ASSERT_EQ(all.size(), 1U);  // updated in place, not duplicated
    EXPECT_EQ(all[0].value, "new");
}

TEST_F(NodeMetadataTest, EmptyValueDeletesKey)
{
    NodeMetadata::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.set(5, "name", "Kitchen light");
    store.set(5, "name", "");  // empty value clears the key

    EXPECT_FALSE(store.get(5, "name").has_value());
}

TEST_F(NodeMetadataTest, RemoveDeletesOnlyThatKey)
{
    NodeMetadata::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.set(5, "name", "Kitchen light");
    store.set(5, "room", "Kitchen");

    store.remove(5, "name");
    EXPECT_FALSE(store.get(5, "name").has_value());
    EXPECT_TRUE(store.get(5, "room").has_value());
}

TEST_F(NodeMetadataTest, GetAllOrderedByKeyAndScopedToNode)
{
    NodeMetadata::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.set(5, "room", "Kitchen");
    store.set(5, "name", "Light");
    store.set(7, "name", "Sensor");

    const auto five = store.getAll(5);
    ASSERT_EQ(five.size(), 2U);
    EXPECT_EQ(five[0].key, "name");  // ordered by key
    EXPECT_EQ(five[1].key, "room");

    const auto seven = store.getAll(7);
    ASSERT_EQ(seven.size(), 1U);
    EXPECT_EQ(seven[0].value, "Sensor");
}

TEST_F(NodeMetadataTest, RequiresHomeAndScopesToIt)
{
    NodeMetadata::Store store(dbPath_);

    // No home bound yet — dropped.
    store.set(5, "name", "x");
    store.setHomeId(kHomeId);
    EXPECT_FALSE(store.get(5, "name").has_value());

    store.set(5, "name", "Kitchen");
    ASSERT_TRUE(store.get(5, "name").has_value());

    // A different home doesn't see node 5's metadata.
    store.setHomeId({0x11, 0x22, 0x33, 0x44});
    EXPECT_FALSE(store.get(5, "name").has_value());
}

TEST_F(NodeMetadataTest, PersistsAcrossRestart)
{
    {
        NodeMetadata::Store first(dbPath_);
        first.setHomeId(kHomeId);
        first.set(5, "name", "Kitchen light");
        first.set(5, "purpose", "main light");
    }  // first's destructor closes the connection

    NodeMetadata::Store second(dbPath_);
    second.setHomeId(kHomeId);
    const auto all = second.getAll(5);
    ASSERT_EQ(all.size(), 2U);
    EXPECT_EQ(*second.get(5, "name"), "Kitchen light");
    EXPECT_EQ(*second.get(5, "purpose"), "main light");
}

TEST_F(NodeMetadataTest, NodesWithTagReverseLookup)
{
    NodeMetadata::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.set(5, "room", "living-room");
    store.set(8, "room", "living-room");
    store.set(3, "room", "living-room");
    store.set(9, "room", "kitchen");
    store.set(8, "name", "TRV");  // a different key on a member shouldn't matter

    // Ascending node id, only exact (key,value) matches.
    EXPECT_EQ(store.nodesWith("room", "living-room"), (std::vector<std::uint8_t>{3, 5, 8}));
    EXPECT_EQ(store.nodesWith("room", "kitchen"), (std::vector<std::uint8_t>{9}));
    EXPECT_TRUE(store.nodesWith("room", "bathroom").empty());     // no such value
    EXPECT_TRUE(store.nodesWith("zone", "living-room").empty());  // no such key
}

TEST_F(NodeMetadataTest, NodesWithIsHomeScoped)
{
    const std::vector<std::uint8_t> otherHome{0x11, 0x22, 0x33, 0x44};
    NodeMetadata::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.set(5, "room", "den");

    store.setHomeId(otherHome);
    EXPECT_TRUE(store.nodesWith("room", "den").empty());  // other network
    store.setHomeId(kHomeId);
    EXPECT_EQ(store.nodesWith("room", "den"), (std::vector<std::uint8_t>{5}));
}

TEST_F(NodeMetadataTest, NodesWithClearedTagDrops)
{
    NodeMetadata::Store store(dbPath_);
    store.setHomeId(kHomeId);
    store.set(5, "room", "den");
    store.set(6, "room", "den");
    store.set(5, "room", "");  // empty value clears the key
    EXPECT_EQ(store.nodesWith("room", "den"), (std::vector<std::uint8_t>{6}));
}
