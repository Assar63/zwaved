#include "MessageBus.hpp"
#include "PolicyRegister.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// Bus-driven test for InclusionOrchestrator (#67). No live dongle: we
// publish a high-level NodeIncluded event and assert on the command
// sequence the orchestrator emits — lifeline SetAssociation, then the
// effective policy (gated on the node's supported CCs), then the
// progress events. The orchestrator arms its subscriptions via
// __attribute__((constructor)) at load; it pulls the effective policy
// from PolicyRegister::instance() and the controller id from the
// retained DongleInfo, both wired up in SetUpTestSuite.

namespace
{
using MessageBus::SubscriptionGuard;

const std::vector<std::uint8_t> kHomeId{0xE2, 0xA1, 0xB0, 0x7C};
constexpr std::uint8_t kController = 1;

// Supported-CC bytes used in NodeIncluded payloads.
constexpr std::uint8_t CC_MULTILEVEL    = 0x26;
constexpr std::uint8_t CC_CONFIGURATION = 0x70;
constexpr std::uint8_t CC_WAKE_UP       = 0x84;
constexpr std::uint8_t CC_ASSOCIATION   = 0x85;

struct Recorder
{
    std::vector<std::string> log;
    std::vector<SubscriptionGuard> guards;

    Recorder()
    {
        guards.emplace_back(MessageBus::subscribe<MessageBus::SetAssociationCommand>(
            [this](const MessageBus::SetAssociationCommand& cmd)
            {
                std::string members;
                for (const auto member : cmd.members)
                {
                    members += std::to_string(member) + ",";
                }
                log.push_back("assoc:" + std::to_string(cmd.nodeId) + ":g" + std::to_string(cmd.groupId) + ":" +
                              members);
            }));
        guards.emplace_back(MessageBus::subscribe<MessageBus::SetConfigurationCommand>(
            [this](const MessageBus::SetConfigurationCommand& cmd)
            {
                log.push_back("config:" + std::to_string(cmd.nodeId) + ":p" + std::to_string(cmd.parameter) + ":" +
                              std::to_string(cmd.value));
            }));
        guards.emplace_back(MessageBus::subscribe<MessageBus::SetWakeUpIntervalCommand>(
            [this](const MessageBus::SetWakeUpIntervalCommand& cmd)
            {
                log.push_back("wakeup:" + std::to_string(cmd.nodeId) + ":" + std::to_string(cmd.seconds) + ":n" +
                              std::to_string(cmd.controllerNodeId));
            }));
        guards.emplace_back(MessageBus::subscribe<MessageBus::InclusionLifelineSet>(
            [this](const MessageBus::InclusionLifelineSet& evt)
            { log.push_back("lifeline:" + std::to_string(evt.nodeId)); }));
        guards.emplace_back(MessageBus::subscribe<MessageBus::InclusionPolicyApplied>(
            [this](const MessageBus::InclusionPolicyApplied& evt)
            { log.push_back("applied:" + std::to_string(evt.nodeId) + ":" + std::to_string(evt.entriesApplied)); }));
        guards.emplace_back(MessageBus::subscribe<MessageBus::InclusionComplete>(
            [this](const MessageBus::InclusionComplete& evt)
            { log.push_back("complete:" + std::to_string(evt.nodeId)); }));
    }
};

class InclusionOrchestratorTest : public ::testing::Test
{
  protected:
    static std::filesystem::path dbPath_;

    static void SetUpTestSuite()
    {
        const auto unique = std::to_string(::getpid()) + "-" + std::to_string(std::random_device{}());
        dbPath_           = std::filesystem::temp_directory_path() / ("zwaved-inclusion-test-" + unique + ".db");
        MessageBus::publish(MessageBus::StorageConfig{.stateDir = dbPath_.parent_path().string()});
        PolicyRegister::instance().setHomeId(kHomeId);
        // Retained — the orchestrator caches the controller id from it.
        MessageBus::publish(MessageBus::DongleInfo{
            .libraryVersion = "test", .libraryType = 0, .homeId = kHomeId, .controllerNodeId = kController});
    }

    static void TearDownTestSuite()
    {
        std::error_code errorCode;
        std::filesystem::remove(dbPath_, errorCode);
    }

    // Each test sets the toggle it wants so order doesn't matter.
    static void setAutoLifeline(bool enabled)
    {
        MessageBus::publish(MessageBus::BehaviorConfig{.autoLifeline = enabled});
    }
};

std::filesystem::path InclusionOrchestratorTest::dbPath_;
}  // namespace

TEST_F(InclusionOrchestratorTest, SetsLifelineForAssociationNodeWithNoPolicy)
{
    setAutoLifeline(true);
    constexpr std::uint8_t node = 9;

    Recorder rec;
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = node, .commandClasses = {CC_ASSOCIATION}});
    MessageBus::publish(MessageBus::NodeInterviewComplete{.nodeId = node});  // policy step waits for this

    ASSERT_EQ(rec.log.size(), 4U);
    EXPECT_EQ(rec.log[0], "assoc:9:g1:1,");  // lifeline → group 1, member = controller (on NodeIncluded)
    EXPECT_EQ(rec.log[1], "lifeline:9");
    EXPECT_EQ(rec.log[2], "applied:9:0");  // no policy entries (on NodeInterviewComplete)
    EXPECT_EQ(rec.log[3], "complete:9");
}

TEST_F(InclusionOrchestratorTest, NoLifelineWhenNodeLacksAssociation)
{
    setAutoLifeline(true);
    constexpr std::uint8_t node = 10;

    Recorder rec;
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = node, .commandClasses = {CC_MULTILEVEL}});
    MessageBus::publish(MessageBus::NodeInterviewComplete{.nodeId = node});

    ASSERT_EQ(rec.log.size(), 2U);
    EXPECT_EQ(rec.log[0], "applied:10:0");
    EXPECT_EQ(rec.log[1], "complete:10");
}

TEST_F(InclusionOrchestratorTest, NoLifelineWhenAutoLifelineDisabled)
{
    setAutoLifeline(false);
    constexpr std::uint8_t node = 11;

    Recorder rec;
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = node, .commandClasses = {CC_ASSOCIATION}});
    MessageBus::publish(MessageBus::NodeInterviewComplete{.nodeId = node});

    ASSERT_EQ(rec.log.size(), 2U);
    EXPECT_EQ(rec.log[0], "applied:11:0");
    EXPECT_EQ(rec.log[1], "complete:11");
    setAutoLifeline(true);  // restore for any later test that races
}

TEST_F(InclusionOrchestratorTest, AppliesPolicyGatedBySupportedCcs)
{
    setAutoLifeline(true);
    constexpr std::uint8_t node = 12;

    // Per-node override: a config param, an extra association, and a
    // wake-up. The node supports Association + Configuration but NOT
    // Wake Up, so the wake-up entry must be skipped.
    PolicyRegister::Policy policy;
    policy.emplace_back(PolicyRegister::ConfigurationEntry{.parameter = 3, .size = 1, .isSigned = false, .value = 42});
    policy.emplace_back(PolicyRegister::AssociationEntry{.groupId = 2, .members = {5}});
    policy.emplace_back(PolicyRegister::WakeUpEntry{.intervalSeconds = 3600, .notificationNodeId = 0});
    PolicyRegister::instance().setNodeOverride(node, policy);

    Recorder rec;
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = node, .commandClasses = {CC_ASSOCIATION, CC_CONFIGURATION}});
    MessageBus::publish(MessageBus::NodeInterviewComplete{.nodeId = node});

    ASSERT_EQ(rec.log.size(), 6U);
    EXPECT_EQ(rec.log[0], "assoc:12:g1:1,");  // lifeline first
    EXPECT_EQ(rec.log[1], "lifeline:12");
    EXPECT_EQ(rec.log[2], "config:12:p3:42");  // policy config
    EXPECT_EQ(rec.log[3], "assoc:12:g2:5,");   // policy association
    // wake-up entry skipped — node doesn't support CC 0x84
    EXPECT_EQ(rec.log[4], "applied:12:2");
    EXPECT_EQ(rec.log[5], "complete:12");
}

TEST_F(InclusionOrchestratorTest, WakeUpPolicyFallsBackToControllerNotifyNode)
{
    setAutoLifeline(true);
    constexpr std::uint8_t node = 13;

    PolicyRegister::Policy policy;
    policy.emplace_back(PolicyRegister::WakeUpEntry{.intervalSeconds = 600, .notificationNodeId = 0});
    PolicyRegister::instance().setNodeOverride(node, policy);

    Recorder rec;
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = node, .commandClasses = {CC_WAKE_UP}});
    MessageBus::publish(MessageBus::NodeInterviewComplete{.nodeId = node});

    // No association CC → no lifeline. Wake-up applied with notify node
    // falling back to the controller id (1).
    ASSERT_EQ(rec.log.size(), 3U);
    EXPECT_EQ(rec.log[0], "wakeup:13:600:n1");
    EXPECT_EQ(rec.log[1], "applied:13:1");
    EXPECT_EQ(rec.log[2], "complete:13");
}
