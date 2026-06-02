#ifndef ZWAVED_POLICY_REGISTER_HPP
#define ZWAVED_POLICY_REGISTER_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

/// Per-device / per-node post-inclusion policy store. Answers: "for this
/// device (or specifically this node), what Configuration parameters /
/// Associations / Wake-Up interval should the daemon set after inclusion
/// (and re-set on wake-up if the node has drifted)?"
///
/// The effective policy for a node is the **device default merged with
/// the per-node override**, with the override winning per entry key
/// (Configuration keyed by `parameter`, Association by `groupId`, Wake-Up
/// is a singleton). InclusionOrchestrator (#67) applies it at inclusion;
/// WakeUpOrchestrator (#68) can re-apply on wake-up.
///
/// Persistent — two tables (`device_policies`, `node_policy_overrides`)
/// in the shared `nodes.db`, on this module's own SQLite connection.
/// Production code uses `PolicyRegister::instance()`; unit tests build
/// `PolicyRegister::Register` directly against a tmp path, same split as
/// PendingQueue.
namespace PolicyRegister
{
/// Set a single Configuration (CC 0x70) parameter.
struct ConfigurationEntry
{
    std::uint8_t parameter = 0;
    std::uint8_t size      = 1;  // 1, 2, or 4
    bool isSigned          = false;
    std::int32_t value     = 0;
};

/// Set an Association (CC 0x85) group's member list.
struct AssociationEntry
{
    std::uint8_t groupId = 0;
    std::vector<std::uint8_t> members;
};

/// Set the Wake Up (CC 0x84) interval. At most one per policy.
struct WakeUpEntry
{
    std::uint32_t intervalSeconds   = 0;
    std::uint8_t notificationNodeId = 0;
};

using PolicyEntry = std::variant<ConfigurationEntry, AssociationEntry, WakeUpEntry>;

/// An ordered list of things to apply to a node. Order is preserved for
/// device entries; merged-in overrides replace matching device entries
/// in place and append the rest.
using Policy = std::vector<PolicyEntry>;

/// Hand-rolled length-prefixed binary form of a policy (stored as the
/// SQLite BLOB). No JSON / protobuf dependency. Format is versioned so
/// it can evolve; `deserialize` returns nullopt on a malformed / unknown
/// blob.
[[nodiscard]] auto serialize(const Policy& policy) -> std::vector<std::uint8_t>;
[[nodiscard]] auto deserialize(const std::vector<std::uint8_t>& bytes) -> std::optional<Policy>;

/// Merge a device default with a per-node override: every override entry
/// replaces the device entry with the same key (Configuration→parameter,
/// Association→groupId, Wake-Up→the single slot) or is appended if there
/// is no match. Device-entry order is preserved.
[[nodiscard]] auto merge(const Policy& deviceDefault, const Policy& nodeOverride) -> Policy;

/// One device's identity, used to key `device_policies` rows.
struct DeviceId
{
    std::uint16_t manufacturerId = 0;
    std::uint16_t productTypeId  = 0;
    std::uint16_t productId      = 0;
};

/// One row of the device-default table — identity + its policy.
struct DevicePolicyRow
{
    DeviceId device;
    Policy policy;
};

class Register
{
  public:
    explicit Register(const std::filesystem::path& dbPath);
    ~Register();

    Register(const Register&)                        = delete;
    auto operator=(const Register&) -> Register&     = delete;
    Register(Register&&) noexcept                    = default;
    auto operator=(Register&&) noexcept -> Register& = default;

    /// Bind per-node operations to a network's 4-byte home ID (same
    /// place ProtocolThread rebinds NodeRegistry / PendingQueue).
    auto setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void;

    /// Upsert a device-default policy keyed by manufacturer triple.
    auto setDevicePolicy(DeviceId device, const Policy& policy) -> void;
    /// Upsert a per-node override (scoped to the bound home ID).
    auto setNodeOverride(std::uint8_t nodeId, const Policy& policy) -> void;
    auto deleteDevicePolicy(DeviceId device) -> void;
    auto deleteNodeOverride(std::uint8_t nodeId) -> void;

    [[nodiscard]] auto devicePolicy(DeviceId device) const -> std::optional<Policy>;
    [[nodiscard]] auto nodeOverride(std::uint8_t nodeId) const -> std::optional<Policy>;

    /// Every device-default policy, in no particular order. Powers the
    /// D-Bus ListDevicePolicies method (#69).
    [[nodiscard]] auto listDevicePolicies() const -> std::vector<DevicePolicyRow>;

    /// Record a node's manufacturer triple (learned from a
    /// ManufacturerSpecific Report). Lets `effectivePolicy` find the
    /// device default without the caller knowing the identity. In-memory
    /// only — re-learnable, and NodeRegistry doesn't persist it today.
    auto noteDeviceIdentity(std::uint8_t nodeId, DeviceId device) -> void;

    /// Effective policy for a node: per-node override merged onto the
    /// device default (if the node's identity is known via
    /// `noteDeviceIdentity`). If identity is unknown, returns the
    /// override alone; if neither exists, an empty policy.
    [[nodiscard]] auto effectivePolicy(std::uint8_t nodeId) const -> Policy;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

/// Production singleton — opens `${state_dir}/nodes.db` (state dir from
/// the retained `StorageConfig` event) and subscribes to
/// `ManufacturerSpecificReport` to keep the device-identity cache warm.
[[nodiscard]] auto instance() -> Register&;
}  // namespace PolicyRegister

#endif  // ZWAVED_POLICY_REGISTER_HPP
