// SpanStore (#199): durable per-peer S2 SPAN state. Two Store instances against
// one file model a daemon restart; home scoping and removal are covered too.

#include "Encapsulation.hpp"
#include "NonceSync.hpp"
#include "SpanManager.hpp"
#include "SpanStore.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
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

// ---- End-to-end: a restart through SQLite resumes in lockstep -------
//
// The unit above proves rows survive; SpanManager's own test proves an
// in-memory export/import stays in lockstep. This is the join of the two —
// the actual #199 promise: a daemon restart reloads SPANs from nodes.db and
// the next encrypted frame decrypts without a Nonce-Sync (SOS) round-trip.

namespace
{
constexpr std::uint8_t CONTROLLER = 1;
constexpr std::uint8_t NODE       = 4;
const S2::Crypto::Key CLASS_KEY{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
const S2::SPAN::Personalization PERS{0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
                                     0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
                                     0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
const std::array<std::uint8_t, 4> HOME_BYTES{0xDE, 0xAD, 0xBE, 0xEF};

auto counterEntropy(std::uint8_t seed) -> S2::SpanManager::EntropySource
{
    auto counter = std::make_shared<std::uint8_t>(seed);
    return [counter]() -> S2::SPAN::EntropyInput
    {
        S2::SPAN::EntropyInput entropy{};
        entropy.fill(*counter);
        *counter += 1;
        return entropy;
    };
}

auto makeManager(std::uint8_t entropySeed, std::uint8_t ours, std::uint8_t peer) -> S2::SpanManager
{
    S2::SpanManager manager(counterEntropy(entropySeed));
    manager.configurePeer(peer,
                          S2::SpanManager::PeerConfig{.classKey        = CLASS_KEY,
                                                      .personalization = PERS,
                                                      .homeId          = HOME_BYTES,
                                                      .ourNodeId       = ours,
                                                      .peerNodeId      = peer});
    return manager;
}
}  // namespace

TEST(SpanStore, PersistedSpanResumesInLockstepAfterRestart)
{
    const auto path = tempDb("zwaved_span_store_lockstep.db");

    auto node = makeManager(0x80, NODE, CONTROLLER);

    // --- first run: establish a SPAN, then checkpoint it to disk ---
    {
        auto controller = makeManager(0x01, CONTROLLER, NODE);
        controller.acceptNonceReport(
            NODE, *S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(node.respondToNonceGet(CONTROLLER))));
        const std::vector<std::uint8_t> first{0x25, 0x01, 0xFF};
        const auto firstFrame = controller.encrypt(NODE, std::span<const std::uint8_t>(first));
        ASSERT_TRUE(firstFrame.has_value());
        ASSERT_TRUE(node.receiveNonce(CONTROLLER, std::span<const std::uint8_t>(*firstFrame)).has_value());

        // What SpanStoreService's checkpoint does: exportAll() → save().
        SpanStore::Store store(path);
        store.setHomeId(HOME_A);
        for (const auto& [peer, inner] : controller.exportAll())
        {
            store.save(peer, inner);
        }
    }  // "daemon stops"

    // --- second run: reload from disk into a fresh manager ---
    auto restarted = makeManager(0x02, CONTROLLER, NODE);
    {
        SpanStore::Store store(path);
        store.setHomeId(HOME_A);
        const auto persisted = store.loadAll();
        ASSERT_EQ(persisted.size(), 1U);
        for (const auto& [peer, inner] : persisted)
        {
            restarted.importSpan(peer, inner);
        }
    }
    ASSERT_TRUE(restarted.hasSpan(NODE));

    // The node never restarted, so it is still on its own SPAN. If persistence
    // worked, the restored controller's next frame decrypts with no resync.
    const std::vector<std::uint8_t> second{0x25, 0x01, 0x00};
    const auto secondFrame = restarted.encrypt(NODE, std::span<const std::uint8_t>(second));
    ASSERT_TRUE(secondFrame.has_value());
    const auto nonce = node.receiveNonce(CONTROLLER, std::span<const std::uint8_t>(*secondFrame));
    ASSERT_TRUE(nonce.has_value());
    const auto inner = S2::Encapsulation::decrypt(std::span<const std::uint8_t>(*secondFrame),
                                                  S2::Encapsulation::Context{.senderNodeId   = CONTROLLER,
                                                                             .receiverNodeId = NODE,
                                                                             .homeId         = HOME_BYTES,
                                                                             .sequenceNumber = (*secondFrame)[2]},
                                                  CLASS_KEY,
                                                  *nonce);
    ASSERT_TRUE(inner.has_value());
    EXPECT_EQ(*inner, second);
}

// The negative control: without the persisted SPAN the same frame does NOT
// decrypt, so the test above is actually proving persistence rather than
// passing for some unrelated reason.
TEST(SpanStore, WithoutPersistedSpanTheRestartCannotResume)
{
    auto node       = makeManager(0x80, NODE, CONTROLLER);
    auto controller = makeManager(0x01, CONTROLLER, NODE);
    controller.acceptNonceReport(
        NODE, *S2::NonceSync::decodeNonceReport(std::span<const std::uint8_t>(node.respondToNonceGet(CONTROLLER))));
    const std::vector<std::uint8_t> first{0x25, 0x01, 0xFF};
    const auto firstFrame = controller.encrypt(NODE, std::span<const std::uint8_t>(first));
    ASSERT_TRUE(firstFrame.has_value());
    ASSERT_TRUE(node.receiveNonce(CONTROLLER, std::span<const std::uint8_t>(*firstFrame)).has_value());

    // Restart with nothing restored — no SPAN, so nothing can be encrypted
    // until a fresh Nonce-Sync round-trip happens.
    auto restarted = makeManager(0x02, CONTROLLER, NODE);
    EXPECT_FALSE(restarted.hasSpan(NODE));
    EXPECT_FALSE(restarted.encrypt(NODE, std::span<const std::uint8_t>(first)).has_value());
}
