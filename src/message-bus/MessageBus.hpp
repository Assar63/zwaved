#ifndef ZWAVED_MESSAGE_BUS_HPP
#define ZWAVED_MESSAGE_BUS_HPP

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

/**
 * In-process publish/subscribe bus for the daemon. Every cross-module
 * conversation rides this bus — state broadcasts, command requests
 * from external transports, and protocol-layer callbacks back to those
 * transports. The bus is the only seam between the threads in
 * src/zwave-protocol, src/zwave-dongle, src/external-api, and
 * src/node-registry; modules do not include each other's headers.
 *
 * Dispatch is synchronous on the publisher's thread; handlers must
 * return promptly and not block. State events flagged retained
 * (see IsRetained) are cached: a new subscriber receives the most
 * recent published value once, immediately, on subscribe — so late
 * starters don't miss the current state. Handlers must not call
 * publish() or subscribe() reentrantly.
 *
 * Adding a new event type:
 *   1. Define the struct here.
 *   2. (Optional) Specialize IsRetained<T> if late subscribers should
 *      receive the latest value on subscribe.
 *   3. Add explicit instantiations in MessageBus.cpp.
 */
namespace MessageBus
{
using SubscriptionId = std::uint64_t;

// ---- State events --------------------------------------------------

/// Lifecycle state of the Z-Wave dongle's serial attachment.
struct DongleStatus
{
    bool connected = false;
    std::string ttyPath;  // Empty unless connected.
};

/// Static introspection captured once when the daemon opens the
/// dongle's serial port: library version string + type, network home
/// ID, and this controller's own node ID.
struct DongleInfo
{
    std::string libraryVersion;
    std::uint8_t libraryType = 0;
    std::vector<std::uint8_t> homeId;  // 4 bytes
    std::uint8_t controllerNodeId = 0;
};

/// FUNC_ID_SERIAL_API_GET_INIT_DATA (0x02) payload, captured once
/// during startup introspection. nodeIds is the expanded node bitmap
/// (every node ID currently included in the network).
struct InitData
{
    std::uint8_t serialApiVersion = 0;
    std::uint8_t capabilities     = 0;
    std::uint8_t chipType         = 0;
    std::uint8_t chipVersion      = 0;
    std::vector<std::uint8_t> nodeIds;
};

/// One node entry in the persistent registry. Mirrors NodeRegistry's
/// internal NodeInfo, redeclared here so external-api consumers don't
/// pull in node-registry headers.
struct NodeInfo
{
    std::uint8_t nodeId       = 0;
    std::uint8_t basicType    = 0;
    std::uint8_t genericType  = 0;
    std::uint8_t specificType = 0;
    std::vector<std::uint8_t> commandClasses;
};

/// Snapshot of the node registry. Republished whenever the registry
/// content changes (add / remove / seed / setHomeId).
struct NodeListChanged
{
    std::vector<NodeInfo> nodes;
};

// ---- Configuration events ----------------------------------------
// Published once at startup by `Config::load()` (priority 102) after
// reading /etc/zwaved/zwaved.conf. Each section becomes one retained
// event; consumers subscribe from their own constructor and pick the
// cached value up via replay-on-subscribe — there is no synchronous
// `Config::snapshot()` accessor, the bus *is* the contract.

/// Logger threshold. `minLevel` is encoded as the underlying integer
/// of `Logger::Level` (Debug=0, Info=1, Warn=2, Error=3) so this
/// header doesn't need to include logger/Logger.hpp.
struct LoggerConfig
{
    std::uint8_t minLevel = 1;  // = Logger::Level::Info
};

/// One accepted USB dongle identity. VID/PID are 4-character lower-
/// case hex strings, compared verbatim against udev's `idVendor` /
/// `idProduct` sysattrs.
struct AcceptedDongleConfig
{
    std::string vid;
    std::string pid;
    std::string name;
};

/// USB dongles the monitor thread will recognise. Defaults to a
/// single entry for the Aeotec Z-Stick Gen5 (0658:0200).
struct DonglesConfig
{
    std::vector<AcceptedDongleConfig> accept;
};

/// Storage paths. `stateDir` empty means "fall back to the legacy
/// resolution: `$ZWAVED_STATE_DIR` env, then `/var/lib/zwaved`".
struct StorageConfig
{
    std::string stateDir;
};

/// Daemon-wide feature toggles.
struct BehaviorConfig
{
    /// When true (default), the protocol thread auto-populates the
    /// lifeline group of a freshly-included Z-Wave Plus node with the
    /// controller's nodeId. See ProtocolThread.cpp::needsAutoLifeline.
    bool autoLifeline = true;
};

// ---- Transient protocol events ------------------------------------

/// Unsolicited command-class frame received from a node, carried inside
/// FUNC_ID_APPLICATION_COMMAND_HANDLER (0x04).
struct ApplicationCommand
{
    std::uint8_t rxStatus     = 0;
    std::uint8_t sourceNodeId = 0;
    std::vector<std::uint8_t> ccData;
};

/// Decoded callback for FUNC_ID_ZW_ADD_NODE_TO_NETWORK (0x4A) — emitted
/// at every step of an inclusion session.
struct NodeInclusionStatus
{
    std::uint8_t sessionId          = 0;
    std::uint8_t status             = 0;
    std::uint16_t nodeId            = 0;
    std::uint8_t basicDeviceType    = 0;
    std::uint8_t genericDeviceType  = 0;
    std::uint8_t specificDeviceType = 0;
    std::vector<std::uint8_t> commandClasses;
};

/// Decoded callback for FUNC_ID_ZW_REMOVE_NODE_FROM_NETWORK (0x4B).
struct NodeExclusionStatus
{
    std::uint8_t sessionId          = 0;
    std::uint8_t status             = 0;
    std::uint16_t nodeId            = 0;
    std::uint8_t basicDeviceType    = 0;
    std::uint8_t genericDeviceType  = 0;
    std::uint8_t specificDeviceType = 0;
    std::vector<std::uint8_t> commandClasses;
};

/// Decoded callback for FUNC_ID_ZW_SEND_DATA (0x13) — emitted once a
/// transmission completes (or fails).
struct SendDataCallback
{
    std::uint8_t callbackId = 0;
    std::uint8_t txStatus   = 0;
};

/// Public mirror of the protocol thread's inclusion/exclusion session
/// tracker. Republished whenever the tracker's begin / end is called,
/// so external observers (DBusBackend's GetNetworkStatus) can answer
/// "is a session in flight" without reaching into protocol internals.
/// Retained — late subscribers see the latest value on subscribe;
/// initial value (no session) is published explicitly when the
/// protocol thread starts.
struct SessionStatus
{
    bool active            = false;
    std::uint8_t commandId = 0;  // HostApi::CMD_ADD_NODE_TO_NETWORK / CMD_REMOVE_NODE_FROM_NETWORK / 0
    std::uint8_t sessionId = 0;
};

/// Progress signal for FUNC_ID_ZW_REMOVE_FAILED_NODE_ID (0x61). Emitted
/// twice for a typical removal: first with phase = PHASE_RESPONSE
/// carrying the dongle's accept/reject (status uses the response-status
/// table), then — only if the response was STARTED — with
/// phase = PHASE_CALLBACK carrying the final outcome (status uses the
/// operation-status table). Synchronous-fail responses (NOT_PRIMARY,
/// NODE_NOT_FOUND, BUSY, FAIL) emit only the first.
struct RemoveFailedNodeStatus
{
    static constexpr std::uint8_t PHASE_RESPONSE = 0;
    static constexpr std::uint8_t PHASE_CALLBACK = 1;

    std::uint8_t nodeId    = 0;
    std::uint8_t sessionId = 0;
    std::uint8_t phase     = PHASE_RESPONSE;
    std::uint8_t status    = 0;  // interpretation depends on phase
};

// ---- Command events (external transports → protocol thread) -------

/// Initiate or stop FUNC_ID_ZW_ADD_NODE_TO_NETWORK (0x4A). Mirrors the
/// wire-level shape; the protocol thread converts it into a Z-Wave
/// data frame.
struct AddNodeCommand
{
    static constexpr std::uint8_t MODE_ANY_NODE            = 0x01;
    static constexpr std::uint8_t MODE_STOP                = 0x05;
    static constexpr std::uint8_t MODE_STOP_REPLICATION    = 0x06;
    static constexpr std::uint8_t MODE_SMART_START_INCLUDE = 0x08;
    static constexpr std::uint8_t MODE_SMART_START_LISTEN  = 0x09;

    std::uint8_t mode      = MODE_ANY_NODE;
    bool power             = false;
    bool nwi               = false;
    bool protocolLongRange = false;
    bool skipFlNeighbors   = false;
    std::uint8_t sessionId = 0;
    bool includeHomeIds    = false;
    std::array<std::uint8_t, 4> nwiHomeId{};
    std::array<std::uint8_t, 4> authHomeId{};
};

/// Initiate or stop FUNC_ID_ZW_REMOVE_NODE_FROM_NETWORK (0x4B).
struct RemoveNodeCommand
{
    static constexpr std::uint8_t MODE_ANY_NODE = 0x01;
    static constexpr std::uint8_t MODE_STOP     = 0x05;

    std::uint8_t mode      = MODE_ANY_NODE;
    bool power             = false;
    bool nwe               = false;
    std::uint8_t sessionId = 0;
};

/// Drive FUNC_ID_ZW_REMOVE_FAILED_NODE_ID (0x61). The target node must
/// already be on the controller's failed-node list (i.e. it has stopped
/// responding) — the dongle answers with NODE_NOT_FOUND otherwise.
struct RemoveFailedNodeCommand
{
    std::uint8_t nodeId    = 0;
    std::uint8_t sessionId = 0;
};

/// Drive a node's Binary Switch (CC 0x25) on/off state.
struct SetSwitchBinaryCommand
{
    std::uint8_t nodeId     = 0;
    bool turnOn             = false;
    std::uint8_t callbackId = 0;
};

/// Set / get a node's Basic Command Class (0x20) value. The value byte
/// follows the spec's universal-fallback semantics: 0x00 = off,
/// 0x01..0x63 = level 1..99, 0xFF = on / full / "previous level".
struct SetBasicCommand
{
    std::uint8_t nodeId     = 0;
    std::uint8_t value      = 0;
    std::uint8_t callbackId = 0;
};

struct GetBasicCommand
{
    std::uint8_t nodeId     = 0;
    std::uint8_t callbackId = 0;
};

/// Set / remove / get / get-groupings on the Association CC (0x85).
struct SetAssociationCommand
{
    std::uint8_t nodeId  = 0;
    std::uint8_t groupId = 0;
    std::vector<std::uint8_t> members;
    std::uint8_t callbackId = 0;
};

struct RemoveAssociationCommand
{
    std::uint8_t nodeId  = 0;
    std::uint8_t groupId = 0;
    std::vector<std::uint8_t> members;
    std::uint8_t callbackId = 0;
};

struct GetAssociationCommand
{
    std::uint8_t nodeId     = 0;
    std::uint8_t groupId    = 0;
    std::uint8_t callbackId = 0;
};

struct GetAssociationGroupingsCommand
{
    std::uint8_t nodeId     = 0;
    std::uint8_t callbackId = 0;
};

/// One node:endpoint pair carried by Multi Channel Association
/// commands and reports. Mirrors
/// `MultichannelAssociation::EndpointMember`; redeclared here so the
/// bus header doesn't pull in the application/ codec.
struct EndpointMember
{
    std::uint8_t nodeId   = 0;
    std::uint8_t endpoint = 0;
};

/// Set / remove / get / get-groupings on the Multi Channel
/// Association CC (0x8E). Like the plain `SetAssociationCommand`
/// trio but each group can hold both whole-node and node:endpoint
/// members.
struct SetMultichannelAssociationCommand
{
    std::uint8_t nodeId  = 0;
    std::uint8_t groupId = 0;
    std::vector<std::uint8_t> nodeMembers;
    std::vector<EndpointMember> endpointMembers;
    std::uint8_t callbackId = 0;
};

struct RemoveMultichannelAssociationCommand
{
    std::uint8_t nodeId  = 0;
    std::uint8_t groupId = 0;
    std::vector<std::uint8_t> nodeMembers;
    std::vector<EndpointMember> endpointMembers;
    std::uint8_t callbackId = 0;
};

struct GetMultichannelAssociationCommand
{
    std::uint8_t nodeId     = 0;
    std::uint8_t groupId    = 0;
    std::uint8_t callbackId = 0;
};

struct GetMultichannelAssociationGroupingsCommand
{
    std::uint8_t nodeId     = 0;
    std::uint8_t callbackId = 0;
};

// ---- Retention trait ----------------------------------------------

/// Specialize to true_type for events whose latest value should be
/// replayed to a subscriber on subscribe.
template <typename T> struct IsRetained : std::false_type
{
};
template <> struct IsRetained<DongleStatus> : std::true_type
{
};
template <> struct IsRetained<DongleInfo> : std::true_type
{
};
template <> struct IsRetained<InitData> : std::true_type
{
};
template <> struct IsRetained<NodeListChanged> : std::true_type
{
};
template <> struct IsRetained<SessionStatus> : std::true_type
{
};
template <> struct IsRetained<LoggerConfig> : std::true_type
{
};
template <> struct IsRetained<DonglesConfig> : std::true_type
{
};
template <> struct IsRetained<StorageConfig> : std::true_type
{
};
template <> struct IsRetained<BehaviorConfig> : std::true_type
{
};

// ---- Public API ---------------------------------------------------

template <typename T> [[nodiscard]] auto subscribe(std::function<void(const T&)> handler) -> SubscriptionId;

template <typename T> auto publish(const T& value) -> void;

auto unsubscribe(SubscriptionId subscriptionId) -> void;

/// Force-construct the bus's internal singleton state. Each module that
/// uses MessageBus has its own static singleton whose destructor must
/// outlive the bus — calling `touch()` from the earliest constructor
/// (Logger, priority 101) registers the bus's atexit handler before any
/// module's, so by LIFO destruction the bus is the **last** static
/// teardown to run and joining-thread destructors can still safely
/// call `unsubscribe(...)`.
auto touch() -> void;
}  // namespace MessageBus

#endif  // ZWAVED_MESSAGE_BUS_HPP
