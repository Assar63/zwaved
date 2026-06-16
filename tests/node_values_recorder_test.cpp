// NodeValues recorder (#213): drive typed CC report events through the bus and
// assert the value cache records the rendered value and a NodeValueChanged fires.
// Exercises the production NodeValues::instance() singleton (configured via a
// tmp StorageConfig), since the recorder writes there.

#include "MessageBus.hpp"
#include "NodeValues.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const std::vector<std::uint8_t> HOME{0xDE, 0xAD, 0xBE, 0xEF};
constexpr std::uint8_t NODE = 5;
}  // namespace

TEST(NodeValuesRecorder, RecordsTypedReports)
{
    const auto dir = std::filesystem::temp_directory_path() / "zwaved_node_values_recorder_test";
    std::filesystem::create_directories(dir);
    MessageBus::publish(MessageBus::StorageConfig{.stateDir = dir.string()});
    MessageBus::publish(MessageBus::DongleInfo{.homeId = HOME, .controllerNodeId = 1});

    std::vector<MessageBus::NodeValueChanged> changes;
    auto guard = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeValueChanged>(
        [&](const MessageBus::NodeValueChanged& event) -> void { changes.push_back(event); }));

    // Binary switch on.
    MessageBus::publish(MessageBus::BinarySwitchReport{.sourceNodeId = NODE, .state = 1});
    // Battery 92%.
    MessageBus::publish(MessageBus::BatteryReport{.sourceNodeId = NODE, .level = 92, .lowBattery = false});
    // Multilevel sensor 21.4 (precision 1, raw 214) — type 1.
    MessageBus::publish(MessageBus::SensorMultilevelReport{
        .sourceNodeId = NODE, .sensorType = 1, .scale = 0, .precision = 1, .value = 214});

    auto& cache   = NodeValues::instance();
    const auto sw = cache.get(NODE, "binary_switch");
    ASSERT_TRUE(sw.has_value());
    EXPECT_EQ(sw->value, "On");

    const auto batt = cache.get(NODE, "battery");
    ASSERT_TRUE(batt.has_value());
    EXPECT_EQ(batt->value, "92%");

    const auto sensor = cache.get(NODE, "sensor:1");
    ASSERT_TRUE(sensor.has_value());
    EXPECT_EQ(sensor->value, "21.4");

    // Each report raised a NodeValueChanged carrying the rendered value.
    bool sawBattery = false;
    for (const auto& change : changes)
    {
        if (change.nodeId == NODE && change.valueId == "battery")
        {
            sawBattery = true;
            EXPECT_EQ(change.value, "92%");
        }
    }
    EXPECT_TRUE(sawBattery);
}

TEST(NodeValuesRecorder, LowBatteryRenders)
{
    const auto dir = std::filesystem::temp_directory_path() / "zwaved_node_values_recorder_test";
    std::filesystem::create_directories(dir);
    MessageBus::publish(MessageBus::StorageConfig{.stateDir = dir.string()});
    MessageBus::publish(MessageBus::DongleInfo{.homeId = HOME, .controllerNodeId = 1});

    constexpr std::uint8_t OTHER = 9;
    MessageBus::publish(MessageBus::BatteryReport{.sourceNodeId = OTHER, .level = 0, .lowBattery = true});
    const auto batt = NodeValues::instance().get(OTHER, "battery");
    ASSERT_TRUE(batt.has_value());
    EXPECT_EQ(batt->value, "low");
}
