#ifndef ZWAVED_NODE_REGISTRY_HPP
#define ZWAVED_NODE_REGISTRY_HPP

#include <cstdint>
#include <vector>

/// In-memory registry of currently-included Z-Wave nodes. Populated by the
/// protocol thread when an inclusion completes (status 0x06) and trimmed
/// when an exclusion completes. Static info only — device types and
/// supported command classes captured at inclusion time. Dynamic state
/// (e.g. last known on/off for a Binary Switch) is exposed as it happens
/// via the existing CC-specific D-Bus signals; it is intentionally not
/// duplicated here.
namespace NodeRegistry
{
struct NodeInfo
{
    std::uint8_t nodeId       = 0;
    std::uint8_t basicType    = 0;
    std::uint8_t genericType  = 0;
    std::uint8_t specificType = 0;
    std::vector<std::uint8_t> commandClasses;
};

/// Bind the registry to a Z-Wave network identified by its 4-byte
/// home ID (typically read from FUNC_ID_MEMORY_GET_ID). Subsequent
/// add/remove/seed/snapshot calls operate against this network only.
/// If the home ID changes (different dongle plugged in), the
/// in-memory cache is reloaded from the DB for the new network;
/// rows for the previous network remain in the database, just out
/// of view. No-op if `homeIdBytes` matches the currently bound ID.
auto setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void;

auto add(const NodeInfo& info) -> void;
auto remove(std::uint8_t nodeId) -> void;

/// Insert a placeholder entry for a node ID we know is included
/// (e.g. from FUNC_ID_SERIAL_API_GET_INIT_DATA's bitmap) but for
/// which we have no device-class or CC info yet. No-op if an
/// entry already exists — won't downgrade a fully-populated node.
auto seed(std::uint8_t nodeId) -> void;

/// Overwrite only the device-class triple (basic/generic/specific)
/// of an existing entry, leaving its `commandClasses` intact.
/// Suitable for filling in seeded entries with the answer from
/// FUNC_ID_GET_NODE_PROTOCOL_INFO (0x41), which carries the device
/// class but not the CC list. No-op if no entry exists for `nodeId`.
auto updateDeviceClass(std::uint8_t nodeId,
                       std::uint8_t basicType,
                       std::uint8_t genericType,
                       std::uint8_t specificType) -> void;

/// Thread-safe copy of the current registry, sorted ascending by nodeId.
[[nodiscard]] auto snapshot() -> std::vector<NodeInfo>;
}  // namespace NodeRegistry

#endif  // ZWAVED_NODE_REGISTRY_HPP
