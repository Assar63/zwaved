// NodeValues recorder (#213) — the bus side of the value cache. A constructor-
// armed reactor (priority 204) that maps the typed CC report events the
// cc-translator publishes into NodeValues::instance().record(...), so the cache
// always holds each node's last-known value + the time it arrived. After each
// record it publishes NodeValueChanged so a UI (the node-info view #45) can
// live-update; the full set is read back via GetNodeValues (a later layer).
//
// Bus-only, no I/O of its own. Binds the cache's home from the retained
// DongleInfo (same key the other stores use), so it's self-sufficient without
// ProtocolThread wiring. Adding a CC is "subscribe + render one more report".

#include "../message-bus/MessageBus.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO
#include "NodeValues.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>

namespace
{
constexpr std::uint8_t BYTE_MAX     = 0xFF;  // multilevel "on at last level" / battery "low" sentinel
constexpr std::int64_t DECIMAL_BASE = 10;

// Render `raw / 10^precision` as a decimal string (sensor / setpoint readings).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): raw value vs precision are clearly named at call sites
auto renderScaled(std::int32_t raw, std::uint8_t precision) -> std::string
{
    if (precision == 0)
    {
        return std::to_string(raw);
    }
    const bool negative          = raw < 0;
    const std::int64_t magnitude = std::llabs(static_cast<std::int64_t>(raw));
    std::int64_t scale           = 1;
    for (std::uint8_t i = 0; i < precision; ++i)
    {
        scale *= DECIMAL_BASE;
    }
    const std::int64_t whole = magnitude / scale;
    std::string frac         = std::to_string(magnitude % scale);
    frac.insert(0, precision - frac.size(), '0');  // zero-pad to `precision` digits
    return (negative ? "-" : "") + std::to_string(whole) + "." + frac;
}

auto renderBinaryState(std::uint8_t state) -> std::string
{
    if (state == 0)
    {
        return "Off";
    }
    if (state == 1)
    {
        return "On";
    }
    return "Unknown";
}

auto renderSwitchLevel(std::uint8_t level) -> std::string
{
    if (level == 0)
    {
        return "Off";
    }
    if (level == BYTE_MAX)
    {
        return "On";
    }
    return std::to_string(level) + "%";
}

auto record(std::uint8_t nodeId, const std::string& valueId, const std::string& value) -> void
{
    NodeValues::instance().record(nodeId, valueId, value);
    MessageBus::publish(MessageBus::NodeValueChanged{.nodeId = nodeId, .valueId = valueId, .value = value});
}

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionGuard dongleSub;
    MessageBus::SubscriptionGuard binarySwitchSub;
    MessageBus::SubscriptionGuard multilevelSwitchSub;
    MessageBus::SubscriptionGuard batterySub;
    MessageBus::SubscriptionGuard sensorMultilevelSub;
    MessageBus::SubscriptionGuard sensorBinarySub;
    MessageBus::SubscriptionGuard configurationSub;
    MessageBus::SubscriptionGuard thermostatModeSub;
    MessageBus::SubscriptionGuard thermostatSetpointSub;

    State()                                    = default;
    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
    ~State()                                   = default;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> State&
{
    static State instance;
    return instance;
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startNodeValuesRecorder() -> void
{
    state().dongleSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleInfo>(
        [](const MessageBus::DongleInfo& info) -> void { NodeValues::instance().setHomeId(info.homeId); }));

    state().binarySwitchSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::BinarySwitchReport>(
        [](const MessageBus::BinarySwitchReport& report) -> void
        { record(report.sourceNodeId, "binary_switch", renderBinaryState(report.state)); }));

    state().multilevelSwitchSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::MultilevelSwitchReport>(
            [](const MessageBus::MultilevelSwitchReport& report) -> void
            { record(report.sourceNodeId, "multilevel_switch", renderSwitchLevel(report.currentValue)); }));

    state().batterySub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::BatteryReport>(
        [](const MessageBus::BatteryReport& report) -> void
        {
            const std::string value =
                (report.lowBattery || report.level == BYTE_MAX) ? "low" : std::to_string(report.level) + "%";
            record(report.sourceNodeId, "battery", value);
        }));

    state().sensorMultilevelSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SensorMultilevelReport>(
            [](const MessageBus::SensorMultilevelReport& report) -> void
            {
                record(report.sourceNodeId,
                       "sensor:" + std::to_string(report.sensorType),
                       renderScaled(report.value, report.precision));
            }));

    state().sensorBinarySub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SensorBinaryReport>(
        [](const MessageBus::SensorBinaryReport& report) -> void
        {
            record(report.sourceNodeId,
                   "sensor_binary:" + std::to_string(report.sensorType),
                   report.value != 0 ? "active" : "idle");
        }));

    state().configurationSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ConfigurationReport>(
        [](const MessageBus::ConfigurationReport& report) -> void
        { record(report.sourceNodeId, "config:" + std::to_string(report.parameter), std::to_string(report.value)); }));

    state().thermostatModeSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ThermostatModeReport>(
        [](const MessageBus::ThermostatModeReport& report) -> void
        { record(report.sourceNodeId, "thermostat_mode", std::to_string(report.mode)); }));

    state().thermostatSetpointSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ThermostatSetpointReport>(
            [](const MessageBus::ThermostatSetpointReport& report) -> void
            {
                record(report.sourceNodeId,
                       "setpoint:" + std::to_string(report.setpointType),
                       renderScaled(report.value, report.precision));
            }));
}
}  // namespace
