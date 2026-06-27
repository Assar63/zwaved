#ifndef ZWAVED_DBUS_BACKEND_INTERNAL_HPP
#define ZWAVED_DBUS_BACKEND_INTERNAL_HPP

// IWYU pragma: begin_exports
#include "../message-bus/MessageBus.hpp"
#include "DBusBackend.hpp"
// IWYU pragma: end_exports

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IObject.h>
#include <sdbus-c++/Types.h>

// Implementation-only header for the D-Bus backend. Holds the Impl
// struct definition (so the generated DBusMethods.gen.cpp can reach
// the daemon's cached state and bus-subscription IDs), the D-Bus tuple
// type aliases, and the bus-name / object-path / interface constants.
//
// Don't include from outside src/external-api/ — consumers should
// only see DBusBackend.hpp / IExternalApi.hpp.

namespace ExternalApi
{
inline constexpr const char* BUS_NAME    = "com.tiunda.ZWaved";
inline constexpr const char* OBJECT_PATH = "/com/tiunda/ZWaved";
inline constexpr const char* IFACE_NAME  = "com.tiunda.ZWaved1";

// ---- D-Bus tuple aliases ---------------------------------------------------
// One alias per multi-field return / signal payload. Used by
// hand-written methods (GetVersion / GetNetworkStatus / GetNodes /
// GetDongleInfo / GetInitData) and the generator-emitted method
// bindings; defined here so both sides agree on the wire shape.

// Inbound `a(yy)` parameter element — the sdbus-c++ wire shape that
// the runtime delivers for Multi Channel Association endpoint pairs.
// Custom handlers convert this into vector<MessageBus::EndpointMember>
// before publishing on the bus.
using EndpointPair = sdbus::Struct<std::uint8_t, std::uint8_t>;

using NodeTuple = sdbus::Struct<std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t, std::vector<std::uint8_t>>;

// Full per-node record for the #45 node-info drill-down (GetNodeInfo): the
// node-registry identity + the interview-gathered capabilities + security
// scheme. Field order matches the manifest's GetNodeInfo struct return and
// NodeRegistry::NodeInfo. See the manifest for the wire signature.
using NodeInfoTuple = sdbus::Struct<std::uint8_t,               // nodeId
                                    std::uint8_t,               // basicType
                                    std::uint8_t,               // genericType
                                    std::uint8_t,               // specificType
                                    std::vector<std::uint8_t>,  // commandClasses
                                    std::uint8_t,               // securityScheme
                                    std::uint16_t,              // manufacturerId
                                    std::uint16_t,              // productTypeId
                                    std::uint16_t,              // productId
                                    std::uint8_t,               // libraryType
                                    std::uint8_t,               // protocolVersion
                                    std::uint8_t,               // protocolSubVersion
                                    std::uint8_t,               // applicationVersion
                                    std::uint8_t,               // applicationSubVersion
                                    std::uint8_t,               // endpointCount
                                    bool,                       // endpointsDynamic
                                    bool,                       // endpointsIdentical
                                    std::uint8_t,               // zwavePlusVersion
                                    std::uint8_t,               // roleType
                                    std::uint8_t,               // nodeType
                                    std::uint16_t,              // installerIconType
                                    std::uint16_t>;             // userIconType

using DongleInfoTuple = sdbus::Struct<std::string, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t>;

using InitDataTuple = sdbus::Struct<std::uint8_t, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t, std::uint8_t>;

using DaemonVersionTuple = sdbus::Struct<std::string, std::string>;

// One ListDevicePolicies row: (manufacturerId, productTypeId, productId,
// policy_bytes) — the device identity triple plus the serialized policy
// BLOB. Matches the manifest's a(qqqay) return shape for the method.
using DevicePolicyTuple = sdbus::Struct<std::uint16_t, std::uint16_t, std::uint16_t, std::vector<std::uint8_t>>;

// One GetNodeMetadata row: (key, value). Matches the a(ss) return shape.
using NodeMetadataTuple = sdbus::Struct<std::string, std::string>;

// One GetNodeValues row: (valueId, value, updatedAt-unix-seconds). Matches the
// a(sst) return shape — the node's last-known value cache (#213).
using NodeValueTuple = sdbus::Struct<std::string, std::string, std::uint64_t>;

// One scene action: (targetNodeId, ccPayload). The wire element of the
// a(yay) scene shape — used both inbound (SetScene param) and outbound
// (GetScene return). Custom handlers convert to/from SceneStore::Action.
using SceneActionEntry = sdbus::Struct<std::uint8_t, std::vector<std::uint8_t>>;

// One scene trigger: (source, sourceNodeId, sceneNumber, keyAttribute,
// sceneId). Matches the a(yyyys) ListSceneTriggers return shape. `source`
// is the SceneStore SOURCE_* discriminator (#124).
using SceneTriggerEntry = sdbus::Struct<std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t, std::string>;

using DaemonErrorTuple = sdbus::Struct<std::uint8_t, std::string, std::uint8_t, std::string>;

// GetLogicalThermostatState return: (memberCount, mode, operatingState,
// fanMode, setpointType, setpointScale, setpointPrecision, setpointValue) —
// the aggregated logical-thermostat state (#134). mode/fanMode carry 0xFF
// when members disagree.
using LogicalThermostatStateTuple = sdbus::Struct<std::uint8_t,
                                                  std::uint8_t,
                                                  std::uint8_t,
                                                  std::uint8_t,
                                                  std::uint8_t,
                                                  std::uint8_t,
                                                  std::uint8_t,
                                                  std::int32_t>;

using NetworkStatusTuple = sdbus::Struct<bool,
                                         std::string,
                                         std::string,
                                         std::uint8_t,
                                         std::uint32_t,
                                         bool,
                                         std::uint8_t,
                                         std::uint8_t,
                                         std::uint64_t>;

// ---- Pimpl state -----------------------------------------------------------
struct DBusBackend::Impl
{
    std::unique_ptr<sdbus::IConnection> connection;
    std::unique_ptr<sdbus::IObject> object;
    std::atomic<bool> connected{false};

    // Hand-written cache-update subscribers. These lambdas live in
    // DBusBackend.cpp; they update impl->last* fields so the
    // `custom: emitGet*` handlers can return cached state. Each guard
    // auto-unsubscribes on destruction — clearing the vector in stop()
    // tears them all down, and the Impl destructor inherits the same
    // teardown as a safety net.
    std::vector<MessageBus::SubscriptionGuard> cacheSubs;

    // Generated signal-emission subscribers, populated by
    // subscribeGeneratedSignals() in DBusSignals.gen.cpp and released
    // by unsubscribeGeneratedSignals() in stop().
    std::vector<MessageBus::SubscriptionId> generatedSignalSubs;

    // Cached state replayed to D-Bus method callers (GetDongleInfo,
    // GetInitData, GetNodes, GetNetworkStatus). Each is fed by a
    // retained MessageBus event so a late D-Bus client gets the
    // latest value without the backend reaching into the producing
    // module.
    std::mutex stateMutex;
    MessageBus::DongleStatus lastDongleStatus;
    MessageBus::DongleInfo lastDongleInfo;
    MessageBus::InitData lastInitData;
    std::vector<MessageBus::NodeInfo> lastNodes;
    MessageBus::SessionStatus lastSessionStatus;
    MessageBus::DaemonError lastDaemonError;

    // Per-group cache of the latest LogicalThermostatState (#134), keyed by
    // (groupKey, groupValue), fed by a cache subscriber. GetLogicalThermostat
    // State reads it; a group not yet addressed+reported returns zeros.
    std::map<std::pair<std::string, std::string>, MessageBus::LogicalThermostatState> lastLogicalThermostat;

    // Captured the first time `run()` is called; powers the uptime
    // field of GetNetworkStatus. steady_clock so it doesn't jump
    // around if the wall clock is stepped.
    std::chrono::steady_clock::time_point startTime;
};
}  // namespace ExternalApi

// IWYU pragma: begin_exports
#include "DBusMethods.gen.hpp"
#include "DBusSignals.gen.hpp"
// IWYU pragma: end_exports

#endif  // ZWAVED_DBUS_BACKEND_INTERNAL_HPP
