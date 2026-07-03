#include "nodes.hpp"

#include "activity.hpp"
#include "constants.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

namespace zwt
{
namespace
{
using NodeTuple      = sdbus::Struct<std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t, std::vector<std::uint8_t>>;
using MetadataTuple  = sdbus::Struct<std::string, std::string>;
using NodeValueTuple = sdbus::Struct<std::string, std::string, std::uint64_t>;

// valueId prefixes in descending interest for the list-column headline; the
// first match wins. Favours actuator state (switch/level) over passive
// readings (sensor/config/battery).
constexpr std::array<const char*, 6> STATE_PRIORITY{
    "binary_switch", "multilevel_switch", "setpoint", "sensor", "config", "battery"};

// The headline value for a node's list-column state (#44): the most
// operationally-relevant of its cached values. Falls back to the first value,
// or empty when the node has reported nothing yet.
auto headlineState(const std::vector<NodeValueRow>& values) -> std::string
{
    for (const auto* prefix : STATE_PRIORITY)
    {
        for (const auto& value : values)
        {
            if (value.valueId.starts_with(prefix))
            {
                return value.value;
            }
        }
    }
    return values.empty() ? std::string{} : values.front().value;
}
}  // namespace

auto nodeModel() -> NodeModel&
{
    static NodeModel instance;
    return instance;
}

auto refreshNodes(sdbus::IProxy& proxy) -> void
{
    std::vector<NodeTuple> nodes;
    try
    {
        proxy.callMethod("GetNodes").onInterface(IFACE_NAME).storeResultsTo(nodes);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetNodes failed: "} + err.what());
        return;
    }

    auto& model                                          = nodeModel();
    const std::optional<std::uint8_t> previouslySelected = selectedNodeId();

    model.rows.clear();
    for (const auto& tup : nodes)
    {
        NodeRow row{.id             = std::get<0>(tup),
                    .basicType      = std::get<1>(tup),
                    .genericType    = std::get<2>(tup),
                    .specificType   = std::get<3>(tup),
                    .commandClasses = std::get<4>(tup),
                    .name           = {},
                    .values         = {}};
        try
        {
            std::vector<MetadataTuple> meta;
            proxy.callMethod("GetNodeMetadata").onInterface(IFACE_NAME).withArguments(row.id).storeResultsTo(meta);
            for (const auto& entry : meta)
            {
                if (std::get<0>(entry) == "name")
                {
                    row.name = std::get<1>(entry);
                }
            }
        }
        // NOLINTNEXTLINE(bugprone-empty-catch): metadata is best-effort decoration; leave the name blank
        catch (const sdbus::Error&)
        {
        }
        model.rows.push_back(std::move(row));
    }

    // Restore the selection to the same node id if it's still present.
    model.selected = 0;
    if (previouslySelected.has_value())
    {
        for (std::size_t i = 0; i < model.rows.size(); ++i)
        {
            if (model.rows.at(i).id == *previouslySelected)
            {
                model.selected = i;
                break;
            }
        }
    }
}

auto refreshValues(sdbus::IProxy& proxy) -> void
{
    for (auto& row : nodeModel().rows)
    {
        std::vector<NodeValueTuple> values;
        try
        {
            proxy.callMethod("GetNodeValues").onInterface(IFACE_NAME).withArguments(row.id).storeResultsTo(values);
        }
        catch (const sdbus::Error& err)
        {
            logLine(std::string{"GetNodeValues failed: "} + err.what());
            continue;  // leave this row's prior values in place; keep going
        }
        row.values.clear();
        for (const auto& tup : values)
        {
            row.values.push_back(
                NodeValueRow{.valueId = std::get<0>(tup), .value = std::get<1>(tup), .updatedAt = std::get<2>(tup)});
        }
        row.state = headlineState(row.values);
    }
}

auto moveSelection(int delta) -> void
{
    auto& model = nodeModel();
    if (model.rows.empty())
    {
        return;
    }
    const int last = static_cast<int>(model.rows.size()) - 1;
    const int next = std::clamp(static_cast<int>(model.selected) + delta, 0, last);
    model.selected = static_cast<std::size_t>(next);
}

auto selectedNodeId() -> std::optional<std::uint8_t>
{
    auto& model = nodeModel();
    if (model.selected >= model.rows.size())
    {
        return std::nullopt;
    }
    return model.rows.at(model.selected).id;
}
}  // namespace zwt
