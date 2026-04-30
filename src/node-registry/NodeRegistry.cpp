#include "NodeRegistry.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace
{
struct State
{
    std::mutex mutex;
    std::map<std::uint8_t, NodeRegistry::NodeInfo> nodes;
};

auto state() -> State&
{
    static State instance;
    return instance;
}
}  // namespace

auto NodeRegistry::add(const NodeInfo& info) -> void
{
    std::lock_guard<std::mutex> const lock(state().mutex);
    state().nodes[info.nodeId] = info;
}

auto NodeRegistry::remove(std::uint8_t nodeId) -> void
{
    std::lock_guard<std::mutex> const lock(state().mutex);
    state().nodes.erase(nodeId);
}

auto NodeRegistry::snapshot() -> std::vector<NodeInfo>
{
    std::lock_guard<std::mutex> const lock(state().mutex);
    std::vector<NodeInfo> result;
    result.reserve(state().nodes.size());
    for (const auto& [_id, info] : state().nodes)
    {
        result.push_back(info);
    }
    return result;
}
