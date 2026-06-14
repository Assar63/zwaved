// SpanStore (#199): durable per-peer S2 SPAN state. Two Store instances against
// one file model a daemon restart; home scoping and removal are covered too.

#include "SpanStore.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const std::vector<std::uint8_t> HOME_A{0xDE, 0xAD, 0xBE, 0xEF};
const std::vector<std::uint8_t> HOME_B{0x11, 0x22, 0x33, 0x44};

auto makeState(std::uint8_t fill) -> S2::SPAN::InnerState
{
    S2::SPAN::InnerState state{};
    state.fill(fill);
    return state;
}

auto tempDb(const char* name) -> std::filesystem::path
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}
}  // namespace

TEST(SpanStore, SurvivesRestart)
{
    const auto path = tempDb("zwaved_span_store_restart.db");
    const auto s5   = makeState(0x55);
    const auto s7   = makeState(0x77);

    {
        SpanStore::Store store(path);
        store.setHomeId(HOME_A);
        store.save(5, s5);
        store.save(7, s7);
    }  // "daemon stops"

    // "daemon starts again" — a fresh Store against the same file.
    SpanStore::Store reopened(path);
    reopened.setHomeId(HOME_A);
    const auto loaded = reopened.loadAll();
    ASSERT_EQ(loaded.size(), 2U);
    EXPECT_EQ(loaded.at(5), s5);
    EXPECT_EQ(loaded.at(7), s7);
}

TEST(SpanStore, SaveReplacesAndRemoveDrops)
{
    const auto path = tempDb("zwaved_span_store_replace.db");
    SpanStore::Store store(path);
    store.setHomeId(HOME_A);

    store.save(9, makeState(0x01));
    store.save(9, makeState(0x02));  // replace
    auto loaded = store.loadAll();
    ASSERT_EQ(loaded.size(), 1U);
    EXPECT_EQ(loaded.at(9), makeState(0x02));

    store.remove(9);
    EXPECT_TRUE(store.loadAll().empty());
}

TEST(SpanStore, ScopedByHome)
{
    const auto path = tempDb("zwaved_span_store_home.db");
    {
        SpanStore::Store store(path);
        store.setHomeId(HOME_A);
        store.save(3, makeState(0xAA));
    }
    SpanStore::Store store(path);
    store.setHomeId(HOME_B);  // a different network
    EXPECT_TRUE(store.loadAll().empty());
    store.setHomeId(HOME_A);
    EXPECT_EQ(store.loadAll().size(), 1U);
}

TEST(SpanStore, NoHomeBoundIsSafe)
{
    const auto path = tempDb("zwaved_span_store_nohome.db");
    SpanStore::Store store(path);
    store.save(1, makeState(0x33));  // ignored — no home bound
    store.setHomeId(HOME_A);
    EXPECT_TRUE(store.loadAll().empty());
}
