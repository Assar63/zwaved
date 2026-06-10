// ThermostatOrchestrator (#131/#133) — a *logical thermostat* over real
// climate devices. A logical thermostat is the set of nodes sharing a
// node-metadata tag `groupKey=groupValue` (#83 reverse lookup, #132) that
// support the relevant Thermostat CC. There is no group store: membership is
// derived from metadata at the moment a command or report is handled.
//
// Two directions (aggregation/mirroring only — closed-loop control is a
// separate future epic):
//   - fan-out: a LogicalThermostat{Mode,Setpoint}Command resolves members
//     via NodeMetadata::nodesWith, gates each on Thermostat CC support
//     (NodeRegistry), and publishes one per-node Set command per member.
//   - mirror: the four typed Thermostat Reports update an in-memory per-node
//     cache; for each *known* group (one a client has addressed) the node
//     belongs to, the aggregate is recomputed and LogicalThermostatState
//     published when it changes.
//
// Loose-coupling rule (same as the other orchestrators): bus-only, owns no
// thread, reacts synchronously under the recursive bus mutex.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../node-metadata/NodeMetadata.hpp"
#include "../node-registry/NodeRegistry.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
// Thermostat CC class bytes (InterfaceManifest module wire constants).
constexpr std::uint8_t CC_THERMOSTAT_MODE            = 0x40;
constexpr std::uint8_t CC_THERMOSTAT_OPERATING_STATE = 0x42;
constexpr std::uint8_t CC_THERMOSTAT_SETPOINT        = 0x43;
constexpr std::uint8_t CC_THERMOSTAT_FAN_MODE        = 0x44;

// Operating-state values (CC 0x42) used for the "any member active" rollup.
constexpr std::uint8_t OP_STATE_IDLE    = 0;
constexpr std::uint8_t OP_STATE_HEATING = 1;
constexpr std::uint8_t OP_STATE_COOLING = 2;

constexpr std::uint8_t NO_CALLBACK = 0x00;

using GroupKey = std::pair<std::string, std::string>;  // (metadata key, value)

struct Setpoint
{
    std::uint8_t type      = 0;
    std::uint8_t scale     = 0;
    std::uint8_t precision = 0;
    std::int32_t value     = 0;
};

// Last-known per-node climate readings, populated from inbound Reports.
struct NodeClimate
{
    std::optional<std::uint8_t> mode;
    std::optional<std::uint8_t> operatingState;
    std::optional<std::uint8_t> fanMode;
    std::optional<Setpoint> setpoint;
    std::uint64_t setpointSeq = 0;  // ordering for "most-recently-reported"
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    std::vector<MessageBus::SubscriptionGuard> subs;

    std::map<std::uint8_t, NodeClimate> perNode;    // by node id
    std::set<GroupKey> knownGroups;                 // groups a client has addressed
    std::map<GroupKey, std::string> lastSignature;  // change-detection per group
    std::uint64_t setpointCounter = 0;

    State() = default;
    ~State()
    {
        Logger::info("[ThermostatOrchestrator] shutdown complete");
    }

    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> State&
{
    static State instance;
    return instance;
}

// True if `nodeId`'s NodeRegistry entry lists `classByte`. Snapshot is taken
// by the caller so a single fan-out does one registry copy.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): nodeId and classByte are clearly named at call sites
auto supportsCc(const std::vector<NodeRegistry::NodeInfo>& nodes, std::uint8_t nodeId, std::uint8_t classByte) -> bool
{
    for (const auto& info : nodes)
    {
        if (info.nodeId == nodeId)
        {
            return std::find(info.commandClasses.begin(), info.commandClasses.end(), classByte) !=
                   info.commandClasses.end();
        }
    }
    return false;
}

// ---- Aggregation across a group's members -------------------------------

// Common value across members that reported one, or MIXED if they disagree,
// or 0 if none reported. `pick` selects the optional field.
template <typename Pick> auto aggregateByte(const std::vector<std::uint8_t>& members, Pick pick) -> std::uint8_t
{
    std::optional<std::uint8_t> common;
    bool mixed = false;
    for (const auto member : members)
    {
        const auto entry = state().perNode.find(member);
        if (entry == state().perNode.end())
        {
            continue;
        }
        const std::optional<std::uint8_t> value = pick(entry->second);
        if (!value.has_value())
        {
            continue;
        }
        if (!common.has_value())
        {
            common = value;
        }
        else if (*common != *value)
        {
            mixed = true;
        }
    }
    if (mixed)
    {
        return MessageBus::LogicalThermostatState::MODE_MIXED;
    }
    return common.value_or(0);
}

// "Active" rollup: heating if any member is heating, else cooling if any is
// cooling, else idle.
auto aggregateOperatingState(const std::vector<std::uint8_t>& members) -> std::uint8_t
{
    bool anyCooling = false;
    for (const auto member : members)
    {
        const auto entry = state().perNode.find(member);
        if (entry == state().perNode.end())
        {
            continue;
        }
        const std::optional<std::uint8_t>& opState = entry->second.operatingState;
        if (!opState.has_value())
        {
            continue;
        }
        if (*opState == OP_STATE_HEATING)
        {
            return OP_STATE_HEATING;
        }
        if (*opState == OP_STATE_COOLING)
        {
            anyCooling = true;
        }
    }
    return anyCooling ? OP_STATE_COOLING : OP_STATE_IDLE;
}

// The most-recently-reported setpoint among members (by sequence).
auto aggregateSetpoint(const std::vector<std::uint8_t>& members) -> Setpoint
{
    Setpoint best;
    std::uint64_t bestSeq = 0;
    for (const auto member : members)
    {
        const auto entry = state().perNode.find(member);
        if (entry == state().perNode.end())
        {
            continue;
        }
        const std::optional<Setpoint>& setpoint = entry->second.setpoint;
        if (setpoint.has_value() && entry->second.setpointSeq >= bestSeq)
        {
            bestSeq = entry->second.setpointSeq;
            best    = *setpoint;
        }
    }
    return best;
}

// Recompute and (if changed) publish the aggregate for one group.
auto publishGroupState(const GroupKey& group) -> void
{
    const std::vector<std::uint8_t> members = NodeMetadata::instance().nodesWith(group.first, group.second);
    const std::uint8_t mode                 = aggregateByte(members, [](const NodeClimate& node) { return node.mode; });
    const std::uint8_t fan       = aggregateByte(members, [](const NodeClimate& node) { return node.fanMode; });
    const std::uint8_t operating = aggregateOperatingState(members);
    const Setpoint setpoint      = aggregateSetpoint(members);

    const MessageBus::LogicalThermostatState event{
        .groupKey          = group.first,
        .groupValue        = group.second,
        .memberCount       = static_cast<std::uint8_t>(members.size()),
        .mode              = mode,
        .operatingState    = operating,
        .fanMode           = fan,
        .setpointType      = setpoint.type,
        .setpointScale     = setpoint.scale,
        .setpointPrecision = setpoint.precision,
        .setpointValue     = setpoint.value,
    };

    // Change-detection so a member report that doesn't move the aggregate
    // stays quiet.
    const std::string signature = std::to_string(event.memberCount) + ":" + std::to_string(event.mode) + ":" +
                                  std::to_string(event.operatingState) + ":" + std::to_string(event.fanMode) + ":" +
                                  std::to_string(event.setpointType) + ":" + std::to_string(event.setpointScale) + ":" +
                                  std::to_string(event.setpointPrecision) + ":" + std::to_string(event.setpointValue);
    auto& last = state().lastSignature[group];
    if (last == signature)
    {
        return;
    }
    last = signature;
    MessageBus::publish(event);
}

// After a member report, refresh every known group the node belongs to.
auto refreshGroupsForNode(std::uint8_t nodeId) -> void
{
    for (const auto& group : state().knownGroups)
    {
        const std::vector<std::uint8_t> members = NodeMetadata::instance().nodesWith(group.first, group.second);
        if (std::find(members.begin(), members.end(), nodeId) != members.end())
        {
            publishGroupState(group);
        }
    }
}

// ---- Fan-out (logical Set -> per-node Sets) ------------------------------

auto onModeCommand(const MessageBus::LogicalThermostatModeCommand& cmd) -> void
{
    state().knownGroups.insert({cmd.groupKey, cmd.groupValue});
    const std::vector<std::uint8_t> members         = NodeMetadata::instance().nodesWith(cmd.groupKey, cmd.groupValue);
    const std::vector<NodeRegistry::NodeInfo> nodes = NodeRegistry::snapshot();
    std::uint32_t fanned                            = 0;
    for (const auto member : members)
    {
        if (!supportsCc(nodes, member, CC_THERMOSTAT_MODE))
        {
            continue;
        }
        MessageBus::publish(
            MessageBus::SetThermostatModeCommand{.nodeId = member, .mode = cmd.mode, .callbackId = NO_CALLBACK});
        ++fanned;
    }
    Logger::info("[ThermostatOrchestrator] mode " + std::to_string(cmd.mode) + " -> group '" + cmd.groupKey + "=" +
                 cmd.groupValue + "' (" + std::to_string(fanned) + " member(s))");
}

auto onSetpointCommand(const MessageBus::LogicalThermostatSetpointCommand& cmd) -> void
{
    state().knownGroups.insert({cmd.groupKey, cmd.groupValue});
    const std::vector<std::uint8_t> members         = NodeMetadata::instance().nodesWith(cmd.groupKey, cmd.groupValue);
    const std::vector<NodeRegistry::NodeInfo> nodes = NodeRegistry::snapshot();
    std::uint32_t fanned                            = 0;
    for (const auto member : members)
    {
        if (!supportsCc(nodes, member, CC_THERMOSTAT_SETPOINT))
        {
            continue;
        }
        MessageBus::publish(MessageBus::SetThermostatSetpointCommand{
            .nodeId       = member,
            .setpointType = cmd.setpointType,
            .precision    = cmd.precision,
            .scale        = cmd.scale,
            .value        = cmd.value,
            .callbackId   = NO_CALLBACK,
        });
        ++fanned;
    }
    Logger::info("[ThermostatOrchestrator] setpoint type " + std::to_string(cmd.setpointType) + " -> group '" +
                 cmd.groupKey + "=" + cmd.groupValue + "' (" + std::to_string(fanned) + " member(s))");
}

// ---- Mirror (per-node report -> logical state) ---------------------------

auto onModeReport(const MessageBus::ThermostatModeReport& report) -> void
{
    state().perNode[report.sourceNodeId].mode = report.mode;
    refreshGroupsForNode(report.sourceNodeId);
}

auto onOperatingStateReport(const MessageBus::ThermostatOperatingStateReport& report) -> void
{
    state().perNode[report.sourceNodeId].operatingState = report.state;
    refreshGroupsForNode(report.sourceNodeId);
}

auto onFanModeReport(const MessageBus::ThermostatFanModeReport& report) -> void
{
    state().perNode[report.sourceNodeId].fanMode = report.mode;
    refreshGroupsForNode(report.sourceNodeId);
}

auto onSetpointReport(const MessageBus::ThermostatSetpointReport& report) -> void
{
    NodeClimate& node = state().perNode[report.sourceNodeId];
    node.setpoint     = Setpoint{
            .type = report.setpointType, .scale = report.scale, .precision = report.precision, .value = report.value};
    node.setpointSeq = ++state().setpointCounter;
    refreshGroupsForNode(report.sourceNodeId);
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startThermostatOrchestrator() -> void
{
    auto& subs = state().subs;
    subs.emplace_back(MessageBus::subscribe<MessageBus::LogicalThermostatModeCommand>(onModeCommand));
    subs.emplace_back(MessageBus::subscribe<MessageBus::LogicalThermostatSetpointCommand>(onSetpointCommand));
    subs.emplace_back(MessageBus::subscribe<MessageBus::ThermostatModeReport>(onModeReport));
    subs.emplace_back(MessageBus::subscribe<MessageBus::ThermostatOperatingStateReport>(onOperatingStateReport));
    subs.emplace_back(MessageBus::subscribe<MessageBus::ThermostatFanModeReport>(onFanModeReport));
    subs.emplace_back(MessageBus::subscribe<MessageBus::ThermostatSetpointReport>(onSetpointReport));
    Logger::info("[ThermostatOrchestrator] managing logical thermostats");
}
}  // namespace
