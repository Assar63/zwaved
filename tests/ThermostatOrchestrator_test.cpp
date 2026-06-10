#include "MessageBus.hpp"
#include "NodeMetadata.hpp"
#include "NodeRegistry.hpp"

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

// Bus-driven test for ThermostatOrchestrator (#133). No live dongle: we tag
// nodes via NodeMetadata, register their CC support in NodeRegistry, then
// publish LogicalThermostat*Commands (assert the per-node Set fan-out) and
// Thermostat Reports (assert the aggregated LogicalThermostatState).
//
// The orchestrator arms its subscriptions via __attribute__((constructor)) at
// load, so linking ThermostatOrchestrator.cpp is enough. NodeMetadata uses a
// StorageConfig-resolved DB path; NodeRegistry shares the same nodes.db.
// Singleton state (perNode cache, knownGroups) persists across cases, so each
// test uses distinct node ids and room values to stay independent.

namespace
{
using MessageBus::SubscriptionGuard;

const std::vector<std::uint8_t> kHomeId{0xE2, 0xA1, 0xB0, 0x7C};

constexpr std::uint8_t CC_MODE     = 0x40;
constexpr std::uint8_t CC_SETPOINT = 0x43;

auto registerNode(std::uint8_t nodeId, const std::vector<std::uint8_t>& ccs) -> void
{
    NodeRegistry::add(NodeRegistry::NodeInfo{
        .nodeId = nodeId, .basicType = 0x04, .genericType = 0x08, .specificType = 0x06, .commandClasses = ccs});
}

struct Recorder
{
    std::vector<std::string> log;
    std::vector<SubscriptionGuard> guards;

    Recorder()
    {
        guards.emplace_back(MessageBus::subscribe<MessageBus::SetThermostatModeCommand>(
            [this](const MessageBus::SetThermostatModeCommand& cmd)
            { log.push_back("mode:" + std::to_string(cmd.nodeId) + ":" + std::to_string(cmd.mode)); }));
        guards.emplace_back(MessageBus::subscribe<MessageBus::SetThermostatSetpointCommand>(
            [this](const MessageBus::SetThermostatSetpointCommand& cmd)
            {
                log.push_back("sp:" + std::to_string(cmd.nodeId) + ":" + std::to_string(cmd.setpointType) + ":" +
                              std::to_string(cmd.value));
            }));
        guards.emplace_back(MessageBus::subscribe<MessageBus::LogicalThermostatState>(
            [this](const MessageBus::LogicalThermostatState& evt)
            {
                log.push_back("state:" + evt.groupValue + ":n" + std::to_string(evt.memberCount) + ":mode" +
                              std::to_string(evt.mode) + ":op" + std::to_string(evt.operatingState) + ":sp" +
                              std::to_string(evt.setpointValue));
            }));
    }
};

class ThermostatOrchestratorTest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        const auto unique = std::to_string(::getpid()) + "-" + std::to_string(std::random_device{}());
        dbPath_           = std::filesystem::temp_directory_path() / ("zwaved-thermo-test-" + unique + ".db");
        MessageBus::publish(MessageBus::StorageConfig{.stateDir = dbPath_.parent_path().string()});
        NodeMetadata::instance().setHomeId(kHomeId);
        NodeRegistry::setHomeId(kHomeId);
    }

    static void TearDownTestSuite()
    {
        std::error_code errorCode;
        std::filesystem::remove(dbPath_, errorCode);
    }

    static std::filesystem::path dbPath_;
};

std::filesystem::path ThermostatOrchestratorTest::dbPath_;
}  // namespace

TEST_F(ThermostatOrchestratorTest, ModeFansOutOnlyToSupportingMembers)
{
    // Nodes 5 and 6 are in the living room; 5 is a thermostat, 6 is a plug.
    NodeMetadata::instance().set(5, "room", "lr-mode");
    NodeMetadata::instance().set(6, "room", "lr-mode");
    registerNode(5, {CC_MODE, CC_SETPOINT});
    registerNode(6, {0x25});  // Binary Switch only — must be skipped

    Recorder rec;
    MessageBus::publish(
        MessageBus::LogicalThermostatModeCommand{.groupKey = "room", .groupValue = "lr-mode", .mode = 1});

    ASSERT_EQ(rec.log.size(), 1U);
    EXPECT_EQ(rec.log[0], "mode:5:1");  // only node 5 (supports CC 0x40)
}

TEST_F(ThermostatOrchestratorTest, SetpointFansOutToSupportingMembers)
{
    NodeMetadata::instance().set(10, "room", "lr-sp");
    NodeMetadata::instance().set(11, "room", "lr-sp");
    registerNode(10, {CC_MODE, CC_SETPOINT});
    registerNode(11, {CC_SETPOINT});

    Recorder rec;
    MessageBus::publish(MessageBus::LogicalThermostatSetpointCommand{
        .groupKey = "room", .groupValue = "lr-sp", .setpointType = 1, .precision = 1, .scale = 0, .value = 215});

    ASSERT_EQ(rec.log.size(), 2U);
    EXPECT_EQ(rec.log[0], "sp:10:1:215");
    EXPECT_EQ(rec.log[1], "sp:11:1:215");
}

TEST_F(ThermostatOrchestratorTest, EmptyGroupFansOutNothing)
{
    Recorder rec;
    MessageBus::publish(
        MessageBus::LogicalThermostatModeCommand{.groupKey = "room", .groupValue = "nobody-home", .mode = 2});
    EXPECT_TRUE(rec.log.empty());  // no members → no Set commands
}

TEST_F(ThermostatOrchestratorTest, MirrorsAgreedModeAndDetectsMixed)
{
    NodeMetadata::instance().set(20, "room", "lr-mirror");
    NodeMetadata::instance().set(21, "room", "lr-mirror");
    registerNode(20, {CC_MODE});
    registerNode(21, {CC_MODE});

    Recorder rec;
    // Address the group once so the orchestrator tracks it.
    MessageBus::publish(
        MessageBus::LogicalThermostatModeCommand{.groupKey = "room", .groupValue = "lr-mirror", .mode = 1});
    rec.log.clear();

    // Both report heat (1) → aggregate mode 1.
    MessageBus::publish(MessageBus::ThermostatModeReport{.sourceNodeId = 20, .mode = 1});
    MessageBus::publish(MessageBus::ThermostatModeReport{.sourceNodeId = 21, .mode = 1});
    // 21 switches to cool (2) → aggregate now MIXED (0xFF).
    MessageBus::publish(MessageBus::ThermostatModeReport{.sourceNodeId = 21, .mode = 2});

    ASSERT_EQ(rec.log.size(), 2U);  // first report sets mode1; third makes it mixed (second is a no-op change)
    EXPECT_EQ(rec.log[0], "state:lr-mirror:n2:mode1:op0:sp0");
    EXPECT_EQ(rec.log[1], "state:lr-mirror:n2:mode255:op0:sp0");  // MODE_MIXED
}

TEST_F(ThermostatOrchestratorTest, OperatingStateActiveIfAnyMemberHeating)
{
    NodeMetadata::instance().set(30, "room", "lr-op");
    NodeMetadata::instance().set(31, "room", "lr-op");
    registerNode(30, {CC_MODE});
    registerNode(31, {CC_MODE});

    Recorder rec;
    MessageBus::publish(MessageBus::LogicalThermostatModeCommand{.groupKey = "room", .groupValue = "lr-op", .mode = 1});
    rec.log.clear();

    // 30 idle, 31 heating → group operating state = heating (1).
    MessageBus::publish(MessageBus::ThermostatOperatingStateReport{.sourceNodeId = 30, .state = 0});
    MessageBus::publish(MessageBus::ThermostatOperatingStateReport{.sourceNodeId = 31, .state = 1});

    ASSERT_FALSE(rec.log.empty());
    EXPECT_EQ(rec.log.back(), "state:lr-op:n2:mode0:op1:sp0");
}
