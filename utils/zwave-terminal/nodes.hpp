#ifndef ZWAVE_TERMINAL_NODES_HPP
#define ZWAVE_TERMINAL_NODES_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// sdbus-c++ Message.h uses std::copy_n in a template body; GCC 15's
// -Wtemplate-body needs <algorithm> visible before it. Keep it ahead of the
// sdbus include so any TU including this header compiles (mirrors handlers.hpp).
#include <algorithm>  // IWYU pragma: keep

#include <sdbus-c++/IProxy.h>

// Node-console model for the master/detail layout (#215): the included-node
// list (from GetNodes + node-metadata names) plus the selected node's cached
// values (from GetNodeValues #213). Lives on the main/UI thread only — the
// D-Bus signal handlers run on the async event-loop thread and touch only the
// activity log, never this model — so no locking is needed here.
namespace zwt
{
struct NodeValueRow
{
    std::string valueId;
    std::string value;
    std::uint64_t updatedAt = 0;  // unix seconds
};

struct NodeRow
{
    std::uint8_t id           = 0;
    std::uint8_t basicType    = 0;
    std::uint8_t genericType  = 0;
    std::uint8_t specificType = 0;
    std::vector<std::uint8_t> commandClasses;
    std::string name;                  // node-metadata "name", empty if unset
    std::vector<NodeValueRow> values;  // cached values (all nodes, from GetNodeValues)
    std::string state;                 // headline value for the list column (#44), empty if none
};

struct NodeModel
{
    std::vector<NodeRow> rows;
    std::size_t selected = 0;  // index into rows (clamped)
};

[[nodiscard]] auto nodeModel() -> NodeModel&;

/// Re-fetch the node list (GetNodes + per-node metadata name), preserving the
/// selection by node id where possible. Best-effort: D-Bus errors leave the
/// previous list in place.
auto refreshNodes(sdbus::IProxy& proxy) -> void;

/// Re-fetch every node's cached values (GetNodeValues per node) into its row,
/// and recompute its headline `state` for the live list column (#44). Powers
/// both the always-visible list state column and the detail pane / node-info
/// modal (which read the selected row's `values`).
auto refreshValues(sdbus::IProxy& proxy) -> void;

/// Move the selection by `delta` rows (clamped to the list).
auto moveSelection(int delta) -> void;

/// The selected node's id, or std::nullopt if the list is empty.
[[nodiscard]] auto selectedNodeId() -> std::optional<std::uint8_t>;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_NODES_HPP
