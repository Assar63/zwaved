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
/// The security level a node was bootstrapped at, highest-granted-class first.
/// Numeric values are persisted (the `security_scheme` column) and carried in
/// the NodeSecurityStatus bus event / D-Bus signal.
enum class SecurityScheme : std::uint8_t
{
    None              = 0,  ///< non-secure
    S0                = 1,  ///< Security S0 (CC 0x98)
    S2Unauthenticated = 2,
    S2Authenticated   = 3,
    S2AccessControl   = 4,
};

struct NodeInfo
{
    std::uint8_t nodeId       = 0;
    std::uint8_t basicType    = 0;
    std::uint8_t genericType  = 0;
    std::uint8_t specificType = 0;
    std::vector<std::uint8_t> commandClasses;
    SecurityScheme securityScheme = SecurityScheme::None;  ///< highest secure class (#167 S0, #186 S2)
    // Device identity from the post-inclusion interview (#203), 0 until gathered.
    std::uint16_t manufacturerId = 0;
    std::uint16_t productTypeId  = 0;
    std::uint16_t productId      = 0;
    // Version (CC 0x86) info from the interview (#203), 0 until gathered.
    std::uint8_t libraryType           = 0;
    std::uint8_t protocolVersion       = 0;
    std::uint8_t protocolSubVersion    = 0;
    std::uint8_t applicationVersion    = 0;
    std::uint8_t applicationSubVersion = 0;
    // Multi Channel (CC 0x60) endpoint info from the interview (#203).
    std::uint8_t endpointCount = 0;
    bool endpointsDynamic      = false;
    bool endpointsIdentical    = false;
    // Z-Wave Plus (CC 0x5E) info from the interview (#203).
    std::uint8_t zwavePlusVersion   = 0;
    std::uint8_t roleType           = 0;
    std::uint8_t nodeType           = 0;
    std::uint16_t installerIconType = 0;
    std::uint16_t userIconType      = 0;
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

/// Overwrite only the `commandClasses` list of an existing entry,
/// leaving its device-class triple intact. Suitable for filling in
/// CC-list info that arrives asynchronously via
/// FUNC_ID_APPLICATION_UPDATE (0x49) — whether triggered by an
/// explicit FUNC_ID_REQUEST_NODE_INFO (0x60) call or by an
/// unsolicited NIF from a node that just woke up. No-op if no
/// entry exists for `nodeId`.
auto updateCommandClasses(std::uint8_t nodeId, std::vector<std::uint8_t> commandClasses) -> void;

/// Record the security scheme a node was bootstrapped at (S0 #167 / S2 #186).
/// Updates the in-memory entry and persists it. No-op if no entry exists.
auto setSecurityScheme(std::uint8_t nodeId, SecurityScheme scheme) -> void;

/// The security scheme of `nodeId` (SecurityScheme::None for non-secure or
/// unknown nodes).
[[nodiscard]] auto securityScheme(std::uint8_t nodeId) -> SecurityScheme;

/// Record a node's device-identity triple, learned from the post-inclusion
/// interview's ManufacturerSpecificReport (#203). Updates the in-memory entry
/// and persists it. No-op if no entry exists.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the triple's wire order is fixed
auto setDeviceIdentity(std::uint8_t nodeId,
                       std::uint16_t manufacturerId,
                       std::uint16_t productTypeId,
                       std::uint16_t productId) -> void;

/// Record a node's Version (CC 0x86) report from the post-inclusion interview
/// (#203): Z-Wave library type + protocol/application firmware versions.
/// Updates the in-memory entry and persists it. No-op if no entry exists.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the report's wire order is fixed
auto setVersionInfo(std::uint8_t nodeId,
                    std::uint8_t libraryType,
                    std::uint8_t protocolVersion,
                    std::uint8_t protocolSubVersion,
                    std::uint8_t applicationVersion,
                    std::uint8_t applicationSubVersion) -> void;

/// Record a node's Multi Channel (CC 0x60) End Point report from the interview
/// (#203): how many endpoints it presents and whether they are dynamic /
/// identical. Updates the in-memory entry and persists it. No-op if no entry
/// exists.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the report's wire order is fixed
auto setEndpointInfo(std::uint8_t nodeId, std::uint8_t endpointCount, bool dynamic, bool identical) -> void;

/// Record a node's Z-Wave Plus Info (CC 0x5E) report from the interview (#203):
/// the Z-Wave Plus version, role/node type, and device-database icons.
/// Updates the in-memory entry and persists it. No-op if no entry exists.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the report's wire order is fixed
auto setZWavePlusInfo(std::uint8_t nodeId,
                      std::uint8_t zwavePlusVersion,
                      std::uint8_t roleType,
                      std::uint8_t nodeType,
                      std::uint16_t installerIconType,
                      std::uint16_t userIconType) -> void;

/// Whether `nodeId` is secure at all (any scheme other than None). Used by the
/// outbound send path to decide whether to encapsulate.
[[nodiscard]] auto isSecure(std::uint8_t nodeId) -> bool;

/// Thread-safe copy of the current registry, sorted ascending by nodeId.
[[nodiscard]] auto snapshot() -> std::vector<NodeInfo>;
}  // namespace NodeRegistry

#endif  // ZWAVED_NODE_REGISTRY_HPP
