// NodeValues store (#213): the per-node value cache. Two Store instances against
// one file model a daemon restart; home scoping, upsert, timestamps (injected
// clock) and clear-on-exclusion are covered.

#include "NodeValues.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const std::vector<std::uint8_t> HOME_A{0xDE, 0xAD, 0xBE, 0xEF};
const std::vector<std::uint8_t> HOME_B{0x11, 0x22, 0x33, 0x44};
constexpr std::uint8_t NODE = 5;

auto tempDb(const char* name) -> std::filesystem::path
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

// A clock the test drives explicitly.
auto fixedClock(std::shared_ptr<std::int64_t> now) -> NodeValues::Clock
{
    return [now]() -> std::int64_t { return *now; };
}
}  // namespace

TEST(NodeValues, RecordsAndReadsBackWithTimestamp)
{
    auto now        = std::make_shared<std::int64_t>(1000);
    const auto path = tempDb("zwaved_node_values_basic.db");
    NodeValues::Store store(path, fixedClock(now));
    store.setHomeId(HOME_A);

    store.record(NODE, "binary_switch", "On");
    *now = 1005;
    store.record(NODE, "battery", "92%");

    const auto sw = store.get(NODE, "binary_switch");
    ASSERT_TRUE(sw.has_value());
    EXPECT_EQ(sw->value, "On");
    EXPECT_EQ(sw->updatedAt, 1000);

    const auto all = store.getAll(NODE);
    ASSERT_EQ(all.size(), 2U);  // ordered by value_id: battery, binary_switch
    EXPECT_EQ(all[0].valueId, "battery");
    EXPECT_EQ(all[0].value, "92%");
    EXPECT_EQ(all[0].updatedAt, 1005);
    EXPECT_EQ(all[1].valueId, "binary_switch");
}

TEST(NodeValues, UpsertReplacesValueAndTimestamp)
{
    auto now        = std::make_shared<std::int64_t>(1000);
    const auto path = tempDb("zwaved_node_values_upsert.db");
    NodeValues::Store store(path, fixedClock(now));
    store.setHomeId(HOME_A);

    store.record(NODE, "binary_switch", "On");
    *now = 2000;
    store.record(NODE, "binary_switch", "Off");  // same value_id → replace

    const auto sw = store.get(NODE, "binary_switch");
    ASSERT_TRUE(sw.has_value());
    EXPECT_EQ(sw->value, "Off");
    EXPECT_EQ(sw->updatedAt, 2000);
    EXPECT_EQ(store.getAll(NODE).size(), 1U);
}

TEST(NodeValues, SurvivesRestart)
{
    auto now        = std::make_shared<std::int64_t>(1234);
    const auto path = tempDb("zwaved_node_values_restart.db");
    {
        NodeValues::Store store(path, fixedClock(now));
        store.setHomeId(HOME_A);
        store.record(NODE, "level", "75");
    }  // "daemon stops"

    NodeValues::Store reopened(path, fixedClock(now));
    reopened.setHomeId(HOME_A);
    const auto level = reopened.get(NODE, "level");
    ASSERT_TRUE(level.has_value());
    EXPECT_EQ(level->value, "75");
    EXPECT_EQ(level->updatedAt, 1234);
}

TEST(NodeValues, ScopedByHomeAndClearable)
{
    auto now        = std::make_shared<std::int64_t>(1);
    const auto path = tempDb("zwaved_node_values_home.db");
    NodeValues::Store store(path, fixedClock(now));

    store.setHomeId(HOME_A);
    store.record(NODE, "battery", "50%");
    store.setHomeId(HOME_B);  // a different network
    EXPECT_TRUE(store.getAll(NODE).empty());
    store.setHomeId(HOME_A);
    EXPECT_EQ(store.getAll(NODE).size(), 1U);

    store.clearForNode(NODE);  // e.g. on exclusion
    EXPECT_TRUE(store.getAll(NODE).empty());
}
