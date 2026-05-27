#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../node-registry/NodeRegistry.hpp"
#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority
#include "FrameTransport.hpp"
#include "HostApi.hpp"
#include "HostApiSession.hpp"
#include "SerialPort.hpp"
#include "ZwaveDataFrame.hpp"
#include "application/Association.hpp"
#include "application/Basic.hpp"
#include "application/Battery.hpp"
#include "application/BinarySwitch.hpp"
#include "application/ManufacturerSpecific.hpp"
#include "application/MultichannelAssociation.hpp"
#include "application/MultilevelSwitch.hpp"
#include "application/NodeVersion.hpp"
#include "application/ZWavePlusInfo.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <sys/prctl.h>

namespace
{
constexpr int IDLE_PUMP_TIMEOUT_MS    = 100;
constexpr int REQUEST_WAIT_TIMEOUT_MS = 100;

// Inclusion / exclusion progression status codes. Different controller
// firmwares deliver the final full-info node callback at either 0x05
// (Protocol complete; some Aeotec firmwares stop here) or 0x06
// (Inclusion completed). Both are treated as "node payload arrived"
// for registry purposes, gated on a non-zero nodeId so the StopAddNode
// echo (commonly nodeId=0) doesn't pollute the registry.
constexpr std::uint8_t STATUS_PROTOCOL_DONE = 0x05;
constexpr std::uint8_t STATUS_COMPLETED     = 0x06;

// Per-request deadline for introspection responses (GET_VERSION,
// MEMORY_GET_ID). Real responses arrive in ~10s of ms; budget is
// generous so a slow-booting dongle still answers.
constexpr int INTROSPECTION_TIMEOUT_MS = 2000;

// SendData serialization. The dongle CANs when a new SendData REQUEST
// arrives while it's still routing the previous one, so we wait for
// the SendData completion callback before letting the protocol loop
// pop the next queued request.
constexpr int SEND_DATA_CALLBACK_TIMEOUT_MS = 5000;
// Fallback when callbackId == 0 — the dongle won't emit a completion
// callback, so we just sleep long enough for a typical Z-Wave transmit
// to clear before issuing the next SendData.
constexpr int SEND_DATA_NO_CALLBACK_DELAY_MS = 300;

// CC wire constants used for the Z-Wave Plus auto-lifeline hook.
// COMMAND_CLASS_MARK separates the supported list (CCs the node will
// answer) from the controlled list (CCs it will emit) inside an
// inclusion's NodeInfo. We only count the supported half — a node that
// merely *emits* Association traffic isn't one we can SET on.
constexpr std::uint8_t CC_ZWAVEPLUS_INFO = 0x5E;
constexpr std::uint8_t CC_ASSOCIATION    = 0x85;
constexpr std::uint8_t CC_MARK           = 0xEF;
constexpr std::uint8_t LIFELINE_GROUP_ID = 1;

/// Internal protocol-thread request variant. Bus-command subscribers
/// translate semantic events into one of these and push to the queue;
/// the protocol loop pops and serializes them onto the dongle's serial
/// link via FrameTransport. Not exposed beyond this translation unit —
/// external transports speak in MessageBus events, not host-API wire
/// shapes.
using Request = std::variant<HostApi::AddNodeRequest,
                             HostApi::RemoveNodeRequest,
                             HostApi::RemoveFailedNodeRequest,
                             HostApi::RequestNodeInfoRequest,
                             HostApi::SendDataRequest>;

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct ZwaveProtocolState
{
    std::thread thread;
    std::atomic<bool> running{false};

    // Latest TTY path advertised over the message bus. Populated by the
    // DongleStatus subscription; the protocol thread blocks on pathCv
    // until path is set (or running flips to false).
    std::mutex pathMutex;
    std::condition_variable pathCv;
    std::optional<std::string> path;

    // Internal request queue. Bus-command subscribers translate
    // semantic commands (AddNodeCommand, SetSwitchBinaryCommand, ...)
    // into Request and push here; the protocol loop pops and
    // sends them one at a time so the dongle isn't asked to route two
    // SendData frames in parallel.
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<Request> queue;

    // Tracks the currently-dispatched Remove Failed Node request so the
    // (response, callback) frame pair — neither of which carries nodeId
    // on the wire alongside sessionId — can be reported with the
    // originating nodeId. Only one removal is in flight at a time
    // because the same request queue serializes them.
    std::optional<HostApi::RemoveFailedNodeRequest> activeFailedNodeRemoval;

    // Tracks the currently-dispatched Request Node Info request so the
    // synchronous 1-byte Response can be tagged back with the caller's
    // (nodeId, sessionId). The async APPLICATION_UPDATE that may follow
    // is not correlated by sessionId — it goes out as a separate
    // NodeInfoUpdate event keyed on nodeId only.
    std::optional<HostApi::RequestNodeInfoRequest> activeRequestNodeInfo;

    // Cached from FUNC_ID_MEMORY_GET_ID introspection — needed as the
    // member byte for the Z-Wave Plus auto-lifeline SetAssociation.
    std::optional<std::uint8_t> controllerNodeId;

    // If a freshly-included node advertised CC_ZWAVEPLUS_INFO and
    // CC_ASSOCIATION in its supported list, the protocol thread queues
    // a SetAssociation(group=1, members=[controllerNodeId]) once the
    // inclusion reaches its terminal status. The pending nodeId is
    // captured here on the first callback that carries node info, then
    // dispatched (and cleared) on the terminal callback.
    std::optional<std::uint8_t> pendingLifelineNodeId;

    // Bus subscriptions, released on shutdown. Each guard auto-unsubscribes
    // on destruction — clearing the vector (from `unsubscribeBus` or
    // ultimately from this struct's destructor) tears them all down.
    std::vector<MessageBus::SubscriptionGuard> subscriptions;

    // Cached `[behavior] auto_lifeline` toggle. Defaults to true to
    // match the daemon's pre-config baseline; updated synchronously
    // when the BehaviorConfig subscription fires (replay-on-subscribe).
    std::atomic<bool> autoLifeline{true};

    // Static-state destructor handles teardown — see the comment in
    // ExternalApiThread.cpp for why we can't rely on
    // __attribute__((destructor)) here.
    ~ZwaveProtocolState()
    {
        running = false;
        pathCv.notify_all();
        queueCv.notify_all();
        if (thread.joinable())
        {
            thread.join();
        }
        Logger::info("Z-Wave communication thread shutdown complete");
    }

    ZwaveProtocolState()                                                 = default;
    ZwaveProtocolState(const ZwaveProtocolState&)                        = delete;
    auto operator=(const ZwaveProtocolState&) -> ZwaveProtocolState&     = delete;
    ZwaveProtocolState(ZwaveProtocolState&&) noexcept                    = delete;
    auto operator=(ZwaveProtocolState&&) noexcept -> ZwaveProtocolState& = delete;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> ZwaveProtocolState&
{
    static ZwaveProtocolState instance;
    return instance;
}

auto pushRequest(const Request& request) -> void
{
    {
        std::scoped_lock const lock(state().queueMutex);
        state().queue.push_back(request);
    }
    state().queueCv.notify_one();
}

auto popRequest(int timeoutMs) -> std::optional<Request>
{
    std::unique_lock<std::mutex> lock(state().queueMutex);
    state().queueCv.wait_for(
        lock, std::chrono::milliseconds(timeoutMs), [] { return !state().queue.empty() || !state().running.load(); });
    if (state().queue.empty())
    {
        return std::nullopt;
    }
    auto out = state().queue.front();
    state().queue.pop_front();
    return out;
}

auto clearRequests() -> void
{
    std::scoped_lock const lock(state().queueMutex);
    state().queue.clear();
}

/// Wrap an application-layer payload in a SendDataRequest with the
/// daemon's default transmit options and push it onto the protocol
/// queue. All onXxx bus-command handlers funnel through here so the
/// SendData envelope stays in one place.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): every call site passes (cmd.nodeId, cmd.callbackId, …)
auto pushSendData(std::uint8_t nodeId, std::uint8_t callbackId, std::vector<std::uint8_t> data) -> void
{
    HostApi::SendDataRequest req{};
    req.nodeId     = nodeId;
    req.data       = std::move(data);
    req.txOptions  = HostApi::TRANSMIT_OPTION_DEFAULT;
    req.callbackId = callbackId;
    pushRequest(req);
}

/// True if `commandClasses` (as captured during inclusion) advertises
/// both Z-Wave Plus Info and Association in its *supported* half — the
/// portion before the COMMAND_CLASS_MARK separator. Z-Wave Plus devices
/// ship with an empty lifeline group (group 1) and expect the including
/// controller to populate it; this predicate is the gate for the
/// auto-lifeline hook.
auto needsAutoLifeline(const std::vector<std::uint8_t>& commandClasses) -> bool
{
    bool hasPlus  = false;
    bool hasAssoc = false;
    for (const auto cls : commandClasses)
    {
        if (cls == CC_MARK)
        {
            break;
        }
        if (cls == CC_ZWAVEPLUS_INFO)
        {
            hasPlus = true;
        }
        else if (cls == CC_ASSOCIATION)
        {
            hasAssoc = true;
        }
    }
    return hasPlus && hasAssoc;
}

auto onDongleStatus(const MessageBus::DongleStatus& status) -> void
{
    {
        std::scoped_lock const lock(state().pathMutex);
        if (status.connected && !status.ttyPath.empty())
        {
            state().path = status.ttyPath;
        }
        else
        {
            state().path.reset();
        }
    }
    state().pathCv.notify_all();
}

auto onAddNodeCommand(const MessageBus::AddNodeCommand& cmd) -> void
{
    HostApi::AddNodeRequest req{};
    req.mode              = cmd.mode;
    req.power             = cmd.power;
    req.nwi               = cmd.nwi;
    req.protocolLongRange = cmd.protocolLongRange;
    req.skipFlNeighbors   = cmd.skipFlNeighbors;
    req.sessionId         = cmd.sessionId;
    req.includeHomeIds    = cmd.includeHomeIds;
    req.nwiHomeId         = cmd.nwiHomeId;
    req.authHomeId        = cmd.authHomeId;
    pushRequest(req);
}

auto onRemoveNodeCommand(const MessageBus::RemoveNodeCommand& cmd) -> void
{
    HostApi::RemoveNodeRequest req{};
    req.mode      = cmd.mode;
    req.power     = cmd.power;
    req.nwe       = cmd.nwe;
    req.sessionId = cmd.sessionId;
    pushRequest(req);
}

auto onRemoveFailedNodeCommand(const MessageBus::RemoveFailedNodeCommand& cmd) -> void
{
    HostApi::RemoveFailedNodeRequest req{};
    req.nodeId    = cmd.nodeId;
    req.sessionId = cmd.sessionId;
    pushRequest(req);
}

auto onRequestNodeInfoCommand(const MessageBus::RequestNodeInfoCommand& cmd) -> void
{
    HostApi::RequestNodeInfoRequest req{};
    req.nodeId    = cmd.nodeId;
    req.sessionId = cmd.sessionId;
    pushRequest(req);
}

auto onSetSwitchBinary(const MessageBus::SetSwitchBinaryCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, BinarySwitch::encodeSet(cmd.turnOn));
}

auto onGetSwitchBinary(const MessageBus::GetSwitchBinaryCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, BinarySwitch::encodeGet());
}

auto onSetBasic(const MessageBus::SetBasicCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, Basic::encodeSet(cmd.value));
}

auto onGetBasic(const MessageBus::GetBasicCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, Basic::encodeGet());
}

auto onSetMultilevelSwitch(const MessageBus::SetMultilevelSwitchCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, MultilevelSwitch::encodeSet(cmd.value, cmd.duration));
}

auto onGetMultilevelSwitch(const MessageBus::GetMultilevelSwitchCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, MultilevelSwitch::encodeGet());
}

auto onGetBattery(const MessageBus::GetBatteryCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, Battery::encodeGet());
}

auto onGetManufacturerSpecific(const MessageBus::GetManufacturerSpecificCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, ManufacturerSpecific::encodeGet());
}

auto onGetZWavePlusInfo(const MessageBus::GetZWavePlusInfoCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, ZWavePlusInfo::encodeGet());
}

auto onGetNodeVersion(const MessageBus::GetNodeVersionCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, NodeVersion::encodeGet());
}

auto onSetAssociation(const MessageBus::SetAssociationCommand& cmd) -> void
{
    pushSendData(
        cmd.nodeId, cmd.callbackId, Association::encodeSet(cmd.groupId, std::span<const std::uint8_t>(cmd.members)));
}

auto onRemoveAssociation(const MessageBus::RemoveAssociationCommand& cmd) -> void
{
    pushSendData(
        cmd.nodeId, cmd.callbackId, Association::encodeRemove(cmd.groupId, std::span<const std::uint8_t>(cmd.members)));
}

auto onGetAssociation(const MessageBus::GetAssociationCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, Association::encodeGet(cmd.groupId));
}

/// Translate the bus's `EndpointMember` (visible to all consumers via
/// MessageBus.hpp) into the application-codec's `EndpointMember` —
/// the two structs hold identical data but live in different
/// translation-unit groups, so we copy field-by-field at the boundary.
auto toCodecEndpoints(const std::vector<MessageBus::EndpointMember>& busMembers)
    -> std::vector<MultichannelAssociation::EndpointMember>
{
    std::vector<MultichannelAssociation::EndpointMember> out;
    out.reserve(busMembers.size());
    for (const auto& member : busMembers)
    {
        out.push_back(MultichannelAssociation::EndpointMember{.nodeId = member.nodeId, .endpoint = member.endpoint});
    }
    return out;
}

auto onSetMultichannelAssociation(const MessageBus::SetMultichannelAssociationCommand& cmd) -> void
{
    const auto endpoints = toCodecEndpoints(cmd.endpointMembers);
    pushSendData(
        cmd.nodeId,
        cmd.callbackId,
        MultichannelAssociation::encodeSet(cmd.groupId,
                                           std::span<const std::uint8_t>(cmd.nodeMembers),
                                           std::span<const MultichannelAssociation::EndpointMember>(endpoints)));
}

auto onRemoveMultichannelAssociation(const MessageBus::RemoveMultichannelAssociationCommand& cmd) -> void
{
    const auto endpoints = toCodecEndpoints(cmd.endpointMembers);
    pushSendData(
        cmd.nodeId,
        cmd.callbackId,
        MultichannelAssociation::encodeRemove(cmd.groupId,
                                              std::span<const std::uint8_t>(cmd.nodeMembers),
                                              std::span<const MultichannelAssociation::EndpointMember>(endpoints)));
}

auto onGetMultichannelAssociation(const MessageBus::GetMultichannelAssociationCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, MultichannelAssociation::encodeGet(cmd.groupId));
}

auto onGetMultichannelAssociationGroupings(const MessageBus::GetMultichannelAssociationGroupingsCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, MultichannelAssociation::encodeGroupingsGet());
}

auto onGetAssociationGroupings(const MessageBus::GetAssociationGroupingsCommand& cmd) -> void
{
    pushSendData(cmd.nodeId, cmd.callbackId, Association::encodeGroupingsGet());
}

auto awaitDevicePath() -> std::optional<std::string>
{
    std::unique_lock<std::mutex> lock(state().pathMutex);
    state().pathCv.wait(lock, [] { return state().path.has_value() || !state().running.load(); });
    if (!state().running.load())
    {
        return std::nullopt;
    }
    return state().path;
}

auto isPathSet() -> bool
{
    std::scoped_lock const lock(state().pathMutex);
    return state().path.has_value();
}

/// Translate a HostApi NodeStatusCallback into the per-direction bus
/// event (NodeInclusionStatus or NodeExclusionStatus) and publish.
auto publishNodeStatus(const HostApi::NodeStatusCallback& callback) -> void
{
    if (callback.commandId == HostApi::CMD_REMOVE_NODE_FROM_NETWORK)
    {
        MessageBus::publish(MessageBus::NodeExclusionStatus{.sessionId          = callback.sessionId,
                                                            .status             = callback.status,
                                                            .nodeId             = callback.nodeId,
                                                            .basicDeviceType    = callback.basicDeviceType,
                                                            .genericDeviceType  = callback.genericDeviceType,
                                                            .specificDeviceType = callback.specificDeviceType,
                                                            .commandClasses     = callback.commandClasses});
    }
    else
    {
        MessageBus::publish(MessageBus::NodeInclusionStatus{.sessionId          = callback.sessionId,
                                                            .status             = callback.status,
                                                            .nodeId             = callback.nodeId,
                                                            .basicDeviceType    = callback.basicDeviceType,
                                                            .genericDeviceType  = callback.genericDeviceType,
                                                            .specificDeviceType = callback.specificDeviceType,
                                                            .commandClasses     = callback.commandClasses});
    }
}

struct IntrospectionResult
{
    std::optional<MessageBus::DongleInfo> dongleInfo;
    std::optional<MessageBus::InitData> initData;
    // Per-node device-class triples for nodes the controller knows
    // about — keyed by the bitmap order of `initData->nodeIds`, so
    // the caller iterates these straight into NodeRegistry without a
    // second pass.
    std::vector<std::pair<std::uint8_t, HostApi::NodeProtocolInfoResponse>> nodeProtocolInfo;
};

/// Run the dongle's startup introspection: GET_VERSION (0x15) +
/// MEMORY_GET_ID (0x20) + SERIAL_API_GET_INIT_DATA (0x02), then a
/// per-node GET_NODE_PROTOCOL_INFO (0x41) for every nodeId in the
/// init-data bitmap. Uses a short-lived FrameTransport with its own
/// capturing handler so RESPONSE frames land in local optionals
/// without going through the runtime callback path. GET_VERSION and
/// MEMORY_GET_ID failures are fatal (no DongleInfo); GET_INIT_DATA is
/// best-effort — if it times out or the dongle doesn't support the
/// FUNC_ID, the result just has no initData payload.
// NOLINTBEGIN(readability-function-cognitive-complexity): flat sequence of REQUEST/RESPONSE steps
auto introspectDongle(SerialPort& port) -> IntrospectionResult
{
    using Clock = std::chrono::steady_clock;

    std::optional<HostApi::VersionResponse> version;
    std::optional<HostApi::MemoryIdResponse> identity;
    std::optional<HostApi::InitDataResponse> initData;
    std::optional<HostApi::NodeProtocolInfoResponse> nodeProtocolInfo;

    FrameTransport transport(&port,
                             [&](const ZwaveDataFrame& frame)
                             {
                                 if (auto resp = HostApi::decodeVersion(frame); resp.has_value())
                                 {
                                     version = resp;
                                 }
                                 else if (auto resp = HostApi::decodeMemoryId(frame); resp.has_value())
                                 {
                                     identity = resp;
                                 }
                                 else if (auto resp = HostApi::decodeInitData(frame); resp.has_value())
                                 {
                                     initData = resp;
                                 }
                                 else if (auto resp = HostApi::decodeNodeProtocolInfo(frame); resp.has_value())
                                 {
                                     nodeProtocolInfo = resp;
                                 }
                             });

    auto sendAndAwait = [&](std::uint8_t commandId, auto& slot) -> bool
    {
        ZwaveDataFrame request;
        request.setHeader(ZwaveDataFrame::FrameType::REQUEST, commandId);
        if (!transport.sendRequest(request))
        {
            return false;
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(INTROSPECTION_TIMEOUT_MS);
        while (!slot.has_value() && Clock::now() < deadline)
        {
            transport.pumpOnce(IDLE_PUMP_TIMEOUT_MS);
        }
        return slot.has_value();
    };

    // Per-node version of `sendAndAwait`: encodes a custom REQUEST
    // (rather than the bare-command-byte form sendAndAwait builds) so
    // the request carries the target nodeId. Slot is reset on entry so
    // the prior node's response doesn't satisfy the wait.
    auto fetchNodeProtocolInfo = [&](std::uint8_t nodeId) -> std::optional<HostApi::NodeProtocolInfoResponse>
    {
        nodeProtocolInfo.reset();
        if (!transport.sendRequest(HostApi::encodeGetNodeProtocolInfo(nodeId)))
        {
            return std::nullopt;
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(INTROSPECTION_TIMEOUT_MS);
        while (!nodeProtocolInfo.has_value() && Clock::now() < deadline)
        {
            transport.pumpOnce(IDLE_PUMP_TIMEOUT_MS);
        }
        return nodeProtocolInfo;
    };

    IntrospectionResult result;

    if (!sendAndAwait(HostApi::CMD_GET_VERSION, version))
    {
        Logger::error("[ProtocolThread] GET_VERSION introspection failed");
        MessageBus::publish(MessageBus::DaemonError{
            .severity = MessageBus::DaemonError::SEVERITY_ERROR,
            .source   = "zwave-protocol",
            .code     = MessageBus::DaemonError::CODE_DONGLE_INTROSPECTION_FAILED,
            .message  = "GET_VERSION introspection failed",
        });
        return result;
    }
    if (!sendAndAwait(HostApi::CMD_MEMORY_GET_ID, identity))
    {
        Logger::error("[ProtocolThread] MEMORY_GET_ID introspection failed");
        MessageBus::publish(MessageBus::DaemonError{
            .severity = MessageBus::DaemonError::SEVERITY_ERROR,
            .source   = "zwave-protocol",
            .code     = MessageBus::DaemonError::CODE_DONGLE_INTROSPECTION_FAILED,
            .message  = "MEMORY_GET_ID introspection failed",
        });
        return result;
    }
    if (!sendAndAwait(HostApi::CMD_SERIAL_API_GET_INIT_DATA, initData))
    {
        Logger::warn("[ProtocolThread] SERIAL_API_GET_INIT_DATA introspection failed (continuing)");
    }
    if (!version.has_value() || !identity.has_value())
    {
        return result;
    }

    MessageBus::DongleInfo info;
    info.libraryVersion = version->version;
    info.libraryType    = version->libraryType;
    info.homeId.assign(identity->homeId.begin(), identity->homeId.end());
    info.controllerNodeId = identity->controllerNodeId;
    result.dongleInfo     = info;

    if (initData.has_value())
    {
        MessageBus::InitData payload;
        payload.serialApiVersion = initData->serialApiVersion;
        payload.capabilities     = initData->capabilities;
        payload.chipType         = initData->chipType;
        payload.chipVersion      = initData->chipVersion;
        payload.nodeIds          = initData->nodeIds;
        result.initData          = payload;

        // Per-node device-class fill-in. GET_NODE_PROTOCOL_INFO is a
        // controller-local query — the dongle answers from its own
        // NVM without going on-air — so it works even for sleeping
        // or out-of-range nodes and adds at most a few ms per node.
        // The CC list still requires REQUEST_NODE_INFO + an
        // APPLICATION_UPDATE round-trip, which would block on
        // sleeping nodes; that's a follow-up best done lazily.
        for (const auto nodeId : initData->nodeIds)
        {
            // Skip the controller's own ID — querying it returns the
            // dongle's own protocol info, which we don't surface to
            // the registry (it isn't a node in the GetNodes sense).
            if (nodeId == identity->controllerNodeId)
            {
                continue;
            }
            const auto info = fetchNodeProtocolInfo(nodeId);
            if (!info.has_value())
            {
                Logger::warn("[ProtocolThread] GET_NODE_PROTOCOL_INFO timeout for node " +
                             std::to_string(static_cast<int>(nodeId)) + " (continuing)");
                continue;
            }
            // All-zero payload means the controller has no entry for
            // this nodeId — which contradicts the init-data bitmap,
            // but treat as "skip" rather than registering a fake
            // device class of 0/0/0.
            if (info->basicDeviceType == 0 && info->genericDeviceType == 0 && info->specificDeviceType == 0)
            {
                continue;
            }
            result.nodeProtocolInfo.emplace_back(nodeId, *info);
        }
    }
    return result;
}
// NOLINTEND(readability-function-cognitive-complexity)

/// Handle the response/callback frame pair for FUNC_ID_ZW_REMOVE_FAILED_NODE_ID.
/// Returns true if the frame matched either decoder (response or callback)
/// and was consumed; false otherwise.
auto handleRemoveFailedNodeFrame(const ZwaveDataFrame& frame) -> bool
{
    if (const auto resp = HostApi::decodeRemoveFailedNodeResponse(frame); resp.has_value())
    {
        const auto pending = state().activeFailedNodeRemoval;
        MessageBus::publish(MessageBus::RemoveFailedNodeStatus{
            .nodeId    = pending.has_value() ? pending->nodeId : static_cast<std::uint8_t>(0),
            .sessionId = pending.has_value() ? pending->sessionId : static_cast<std::uint8_t>(0),
            .phase     = MessageBus::RemoveFailedNodeStatus::PHASE_RESPONSE,
            .status    = resp->status});
        if (resp->status != HostApi::RemoveFailedNodeResponse::STATUS_STARTED)
        {
            // Synchronous failure: no callback will follow, so close
            // out the in-flight slot now.
            state().activeFailedNodeRemoval.reset();
        }
        return true;
    }
    if (const auto callback = HostApi::decodeRemoveFailedNodeCallback(frame); callback.has_value())
    {
        const auto pending = state().activeFailedNodeRemoval;
        const auto nodeId  = pending.has_value() ? pending->nodeId : static_cast<std::uint8_t>(0);
        MessageBus::publish(
            MessageBus::RemoveFailedNodeStatus{.nodeId    = nodeId,
                                               .sessionId = callback->sessionId,
                                               .phase     = MessageBus::RemoveFailedNodeStatus::PHASE_CALLBACK,
                                               .status    = callback->status});
        if (callback->status == HostApi::RemoveFailedNodeCallback::STATUS_REMOVED && nodeId != 0)
        {
            NodeRegistry::remove(nodeId);
        }
        state().activeFailedNodeRemoval.reset();
        return true;
    }
    return false;
}

/// Handle the synchronous RESPONSE for FUNC_ID_ZW_REQUEST_NODE_INFO.
/// Returns true if the frame matched. The async NodeInfo (when / if
/// the node answers) is handled by handleApplicationUpdateFrame —
/// `accepted=false` here means we won't get one for this request.
auto handleRequestNodeInfoResponse(const ZwaveDataFrame& frame) -> bool
{
    const auto resp = HostApi::decodeRequestNodeInfoResponse(frame);
    if (!resp.has_value())
    {
        return false;
    }
    const auto pending = state().activeRequestNodeInfo;
    MessageBus::publish(MessageBus::RequestNodeInfoStatus{
        .nodeId    = pending.has_value() ? pending->nodeId : static_cast<std::uint8_t>(0),
        .sessionId = pending.has_value() ? pending->sessionId : static_cast<std::uint8_t>(0),
        .accepted  = resp->accepted,
    });
    // Done with the in-flight slot either way — the async
    // APPLICATION_UPDATE is uncorrelated by sessionId.
    state().activeRequestNodeInfo.reset();
    return true;
}

/// Handle an asynchronous FUNC_ID_APPLICATION_UPDATE frame. Returns
/// true if the frame matched. On STATUS_NODE_INFO_RECEIVED, also
/// updates NodeRegistry with the device-class triple + CC list.
auto handleApplicationUpdateFrame(const ZwaveDataFrame& frame) -> bool
{
    const auto update = HostApi::decodeApplicationUpdate(frame);
    if (!update.has_value())
    {
        return false;
    }
    MessageBus::publish(MessageBus::NodeInfoUpdate{
        .status             = update->status,
        .nodeId             = update->nodeId,
        .basicDeviceType    = update->basicDeviceType,
        .genericDeviceType  = update->genericDeviceType,
        .specificDeviceType = update->specificDeviceType,
        .commandClasses     = update->commandClasses,
    });
    if (update->status == HostApi::ApplicationUpdate::STATUS_NODE_INFO_RECEIVED && update->nodeId != 0)
    {
        NodeRegistry::updateDeviceClass(
            update->nodeId, update->basicDeviceType, update->genericDeviceType, update->specificDeviceType);
        if (!update->commandClasses.empty())
        {
            NodeRegistry::updateCommandClasses(update->nodeId, update->commandClasses);
        }
    }
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): flat per-FUNC_ID dispatch; splintering doesn't help
auto handleIncomingFrame(HostApi::SessionTracker& tracker, const ZwaveDataFrame& frame) -> void
{
    if (const auto sendDataCb = HostApi::decodeSendDataCallback(frame); sendDataCb.has_value())
    {
        MessageBus::publish(
            MessageBus::SendDataCallback{.callbackId = sendDataCb->callbackId, .txStatus = sendDataCb->txStatus});
        return;
    }
    if (const auto appCmd = HostApi::decodeApplicationCommand(frame); appCmd.has_value())
    {
        MessageBus::publish(MessageBus::ApplicationCommand{
            .rxStatus = appCmd->rxStatus, .sourceNodeId = appCmd->sourceNodeId, .ccData = appCmd->ccData});
        return;
    }
    if (handleRemoveFailedNodeFrame(frame))
    {
        return;
    }
    if (handleRequestNodeInfoResponse(frame))
    {
        return;
    }
    if (handleApplicationUpdateFrame(frame))
    {
        return;
    }
    if (const auto nodeCb = HostApi::decodeNodeCallback(frame); nodeCb.has_value())
    {
        publishNodeStatus(*nodeCb);

        // Inclusion: most controllers deliver the node's full info (device
        // class triple + supported command classes) at 0x03/0x04 (Inclusion
        // ongoing — End Node / Controller); a thinner 0x05/0x06 follows
        // with just nodeId. Register on the *first* callback that carries
        // payload, irrespective of status, so we capture the CC list. UPSERT
        // makes a later thinner callback for the same node a no-op overwrite.
        if (nodeCb->commandId == HostApi::CMD_ADD_NODE_TO_NETWORK && nodeCb->nodeId != 0)
        {
            const bool hasNodeInfo = !nodeCb->commandClasses.empty() || nodeCb->basicDeviceType != 0 ||
                                     nodeCb->genericDeviceType != 0 || nodeCb->specificDeviceType != 0;
            if (hasNodeInfo)
            {
                NodeRegistry::add({.nodeId         = static_cast<std::uint8_t>(nodeCb->nodeId),
                                   .basicType      = nodeCb->basicDeviceType,
                                   .genericType    = nodeCb->genericDeviceType,
                                   .specificType   = nodeCb->specificDeviceType,
                                   .commandClasses = nodeCb->commandClasses});
                if (needsAutoLifeline(nodeCb->commandClasses))
                {
                    state().pendingLifelineNodeId = static_cast<std::uint8_t>(nodeCb->nodeId);
                }
            }
        }
        // Exclusion: only nodeId is needed; trigger on a session-ending status.
        else if (nodeCb->commandId == HostApi::CMD_REMOVE_NODE_FROM_NETWORK && nodeCb->nodeId != 0 &&
                 (nodeCb->status == STATUS_PROTOCOL_DONE || nodeCb->status == STATUS_COMPLETED))
        {
            NodeRegistry::remove(static_cast<std::uint8_t>(nodeCb->nodeId));
        }

        if (HostApi::isTerminalStatus(nodeCb->commandId, nodeCb->status))
        {
            // Z-Wave Plus auto-lifeline: queue the SetAssociation now that
            // inclusion has reached its terminal step, so the node is
            // ready to answer application-layer commands. Goes through the
            // same request queue as any user-issued request, so it serializes
            // naturally with anything else the dongle is already routing.
            if (const auto pendingNode = state().pendingLifelineNodeId, controller = state().controllerNodeId;
                nodeCb->commandId == HostApi::CMD_ADD_NODE_TO_NETWORK && pendingNode.has_value() &&
                controller.has_value() && state().autoLifeline.load(std::memory_order_relaxed))
            {
                const std::array<std::uint8_t, 1> members{*controller};
                HostApi::SendDataRequest req{};
                req.nodeId     = *pendingNode;
                req.data       = Association::encodeSet(LIFELINE_GROUP_ID, std::span<const std::uint8_t>(members));
                req.txOptions  = HostApi::TRANSMIT_OPTION_DEFAULT;
                req.callbackId = 0;  // fire-and-forget; SendDataStatus would only be noise
                pushRequest(req);
                Logger::info("[ProtocolThread] auto-lifeline: SetAssociation node=" +
                             std::to_string(static_cast<int>(*pendingNode)) +
                             " group=1 controller=" + std::to_string(static_cast<int>(*controller)));
            }
            state().pendingLifelineNodeId.reset();
            tracker.end();
            MessageBus::publish(MessageBus::SessionStatus{.active = false, .commandId = 0, .sessionId = 0});
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): flat per-variant dispatch
auto dispatchRequest(FrameTransport& transport,
                     HostApi::SessionTracker& tracker,
                     std::optional<std::uint8_t>& pendingSendData,
                     const Request& request) -> void
{
    std::visit(
        // NOLINTNEXTLINE(readability-function-cognitive-complexity): per-variant dispatch, see enclosing fn comment
        [&]<typename T0>(T0 const& concrete) -> auto
        {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, HostApi::AddNodeRequest>)
            {
                ZwaveDataFrame const frame = HostApi::encodeAddNode(concrete);
                if (concrete.mode != HostApi::ADD_MODE_STOP && concrete.mode != HostApi::ADD_MODE_STOP_REPLICATION)
                {
                    tracker.begin(HostApi::CMD_ADD_NODE_TO_NETWORK, concrete.sessionId);
                    MessageBus::publish(MessageBus::SessionStatus{.active    = true,
                                                                  .commandId = HostApi::CMD_ADD_NODE_TO_NETWORK,
                                                                  .sessionId = concrete.sessionId});
                }
                if (!transport.sendRequest(frame))
                {
                    Logger::error("[ProtocolThread] AddNode send failed (session " +
                                  std::to_string(static_cast<int>(concrete.sessionId)) + ")");
                }
            }
            else if constexpr (std::is_same_v<T, HostApi::RemoveNodeRequest>)
            {
                ZwaveDataFrame const frame = HostApi::encodeRemoveNode(concrete);
                if (concrete.mode != HostApi::REMOVE_MODE_STOP)
                {
                    tracker.begin(HostApi::CMD_REMOVE_NODE_FROM_NETWORK, concrete.sessionId);
                    MessageBus::publish(MessageBus::SessionStatus{.active    = true,
                                                                  .commandId = HostApi::CMD_REMOVE_NODE_FROM_NETWORK,
                                                                  .sessionId = concrete.sessionId});
                }
                if (!transport.sendRequest(frame))
                {
                    Logger::error("[ProtocolThread] RemoveNode send failed (session " +
                                  std::to_string(static_cast<int>(concrete.sessionId)) + ")");
                }
            }
            else if constexpr (std::is_same_v<T, HostApi::RemoveFailedNodeRequest>)
            {
                state().activeFailedNodeRemoval = concrete;
                ZwaveDataFrame const frame      = HostApi::encodeRemoveFailedNode(concrete);
                if (!transport.sendRequest(frame))
                {
                    Logger::error("[ProtocolThread] RemoveFailedNode send failed (node " +
                                  std::to_string(static_cast<int>(concrete.nodeId)) + ", session " +
                                  std::to_string(static_cast<int>(concrete.sessionId)) + ")");
                    state().activeFailedNodeRemoval.reset();
                    MessageBus::publish(MessageBus::RemoveFailedNodeStatus{
                        .nodeId    = concrete.nodeId,
                        .sessionId = concrete.sessionId,
                        .phase     = MessageBus::RemoveFailedNodeStatus::PHASE_RESPONSE,
                        .status    = HostApi::RemoveFailedNodeResponse::STATUS_REMOVE_FAIL});
                }
            }
            else if constexpr (std::is_same_v<T, HostApi::RequestNodeInfoRequest>)
            {
                state().activeRequestNodeInfo = concrete;
                ZwaveDataFrame const frame    = HostApi::encodeRequestNodeInfo(concrete);
                if (!transport.sendRequest(frame))
                {
                    Logger::error("[ProtocolThread] RequestNodeInfo send failed (node " +
                                  std::to_string(static_cast<int>(concrete.nodeId)) + ", session " +
                                  std::to_string(static_cast<int>(concrete.sessionId)) + ")");
                    state().activeRequestNodeInfo.reset();
                    // Synthesize a Response saying "not accepted" so the
                    // external API doesn't hang waiting for a signal.
                    MessageBus::publish(MessageBus::RequestNodeInfoStatus{
                        .nodeId    = concrete.nodeId,
                        .sessionId = concrete.sessionId,
                        .accepted  = false,
                    });
                }
            }
            else if constexpr (std::is_same_v<T, HostApi::SendDataRequest>)
            {
                ZwaveDataFrame const frame = HostApi::encodeSendData(concrete);
                if (!transport.sendRequest(frame))
                {
                    Logger::error("[ProtocolThread] SendData send failed (node " +
                                  std::to_string(static_cast<int>(concrete.nodeId)) + ", callback " +
                                  std::to_string(static_cast<int>(concrete.callbackId)) + ")");
                    // Synthesize a failure callback so the external API gets
                    // notified instead of waiting indefinitely for a response.
                    MessageBus::publish(MessageBus::SendDataCallback{.callbackId = concrete.callbackId,
                                                                     .txStatus   = HostApi::TRANSMIT_COMPLETE_FAIL});
                    return;
                }
                // Throttle back-to-back SendData: the dongle will CAN any new
                // frame while it's still routing the previous one. Wait for
                // the completion callback (cleared by the transport handler
                // in runConnectedSession) — or fall back to a fixed delay if
                // the caller asked for no callback (callbackId == 0).
                if (concrete.callbackId != 0)
                {
                    pendingSendData     = concrete.callbackId;
                    using Clock         = std::chrono::steady_clock;
                    const auto deadline = Clock::now() + std::chrono::milliseconds(SEND_DATA_CALLBACK_TIMEOUT_MS);
                    while (pendingSendData.has_value() && Clock::now() < deadline && state().running.load())
                    {
                        transport.pumpOnce(IDLE_PUMP_TIMEOUT_MS);
                    }
                    pendingSendData.reset();
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(SEND_DATA_NO_CALLBACK_DELAY_MS));
                }
            }
        },
        request);
}

auto runConnectedSession(SerialPort& port) -> void
{
    HostApi::SessionTracker tracker;
    std::optional<std::uint8_t> pendingSendData;

    FrameTransport transport(&port,
                             [&](const ZwaveDataFrame& frame) -> void
                             {
                                 // Clear the pending-SendData gate as soon as the matching
                                 // completion callback arrives. handleIncomingFrame will also
                                 // publish the callback to the bus.
                                 if (pendingSendData.has_value())
                                 {
                                     if (const auto callback = HostApi::decodeSendDataCallback(frame);
                                         callback.has_value() && callback->callbackId == *pendingSendData)
                                     {
                                         pendingSendData.reset();
                                     }
                                 }
                                 handleIncomingFrame(tracker, frame);
                             });

    while (state().running && port.isOpen())
    {
        if (!isPathSet())
        {
            Logger::info("[ProtocolThread] device removed, closing serial port");
            port.close();
            break;
        }

        auto request = popRequest(REQUEST_WAIT_TIMEOUT_MS);
        if (request.has_value())
        {
            dispatchRequest(transport, tracker, pendingSendData, *request);
        }
        else
        {
            transport.pumpOnce(IDLE_PUMP_TIMEOUT_MS);
        }
    }
}

/// Bind a handler to a bus event, wrap the subscription ID in a guard,
/// and stash the guard so the protocol thread owns its teardown.
template <typename Event, typename Handler> auto subscribe(Handler&& handler) -> void
{
    state().subscriptions.emplace_back(MessageBus::subscribe<Event>(std::forward<Handler>(handler)));
}

// Count of bus subscriptions registered by `subscribeBus`. Kept in sync
// with the body — used only as a `vector::reserve` hint so off-by-one is
// harmless beyond an extra reallocation.
constexpr std::size_t SUBSCRIPTION_COUNT = 24;

auto subscribeBus() -> void
{
    auto& subs = state().subscriptions;
    subs.reserve(SUBSCRIPTION_COUNT);
    subscribe<MessageBus::DongleStatus>(onDongleStatus);
    subscribe<MessageBus::AddNodeCommand>(onAddNodeCommand);
    subscribe<MessageBus::RemoveNodeCommand>(onRemoveNodeCommand);
    subscribe<MessageBus::RemoveFailedNodeCommand>(onRemoveFailedNodeCommand);
    subscribe<MessageBus::RequestNodeInfoCommand>(onRequestNodeInfoCommand);
    subscribe<MessageBus::SetSwitchBinaryCommand>(onSetSwitchBinary);
    subscribe<MessageBus::GetSwitchBinaryCommand>(onGetSwitchBinary);
    subscribe<MessageBus::SetBasicCommand>(onSetBasic);
    subscribe<MessageBus::GetBasicCommand>(onGetBasic);
    subscribe<MessageBus::SetMultilevelSwitchCommand>(onSetMultilevelSwitch);
    subscribe<MessageBus::GetMultilevelSwitchCommand>(onGetMultilevelSwitch);
    subscribe<MessageBus::GetBatteryCommand>(onGetBattery);
    subscribe<MessageBus::GetManufacturerSpecificCommand>(onGetManufacturerSpecific);
    subscribe<MessageBus::GetZWavePlusInfoCommand>(onGetZWavePlusInfo);
    subscribe<MessageBus::GetNodeVersionCommand>(onGetNodeVersion);
    subscribe<MessageBus::SetAssociationCommand>(onSetAssociation);
    subscribe<MessageBus::RemoveAssociationCommand>(onRemoveAssociation);
    subscribe<MessageBus::GetAssociationCommand>(onGetAssociation);
    subscribe<MessageBus::GetAssociationGroupingsCommand>(onGetAssociationGroupings);
    subscribe<MessageBus::SetMultichannelAssociationCommand>(onSetMultichannelAssociation);
    subscribe<MessageBus::RemoveMultichannelAssociationCommand>(onRemoveMultichannelAssociation);
    subscribe<MessageBus::GetMultichannelAssociationCommand>(onGetMultichannelAssociation);
    subscribe<MessageBus::GetMultichannelAssociationGroupingsCommand>(onGetMultichannelAssociationGroupings);
    subscribe<MessageBus::BehaviorConfig>([](const MessageBus::BehaviorConfig& cfg) -> void
                                          { state().autoLifeline.store(cfg.autoLifeline, std::memory_order_relaxed); });
}

auto unsubscribeBus() -> void
{
    // Guards' destructors call MessageBus::unsubscribe; the State
    // destructor would do the same if the thread exited abnormally.
    state().subscriptions.clear();
}

auto zwaveCommunicationThread() -> void
{
    prctl(PR_SET_NAME, "ZWaveProto", 0, 0, 0);  // NOLINT(misc-include-cleaner): PR_SET_NAME from <sys/prctl.h>

    subscribeBus();
    // Seed the retained SessionStatus with "no session in flight" so
    // subscribers (DBusBackend's GetNetworkStatus path) get a value
    // immediately on subscribe, even before the first inclusion.
    MessageBus::publish(MessageBus::SessionStatus{});

    while (state().running)
    {
        auto path = awaitDevicePath();
        if (!state().running)
        {
            break;
        }
        if (!path.has_value() || path->empty())
        {
            continue;
        }

        SerialPort port;
        if (!port.open(*path))
        {
            Logger::error("[ProtocolThread] failed to open " + *path + "; backing off");
            MessageBus::publish(MessageBus::DaemonError{
                .severity = MessageBus::DaemonError::SEVERITY_ERROR,
                .source   = "zwave-protocol",
                .code     = MessageBus::DaemonError::CODE_DONGLE_OPEN_FAILED,
                .message  = "failed to open serial port " + *path,
            });
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        if (auto introspection = introspectDongle(port); introspection.dongleInfo.has_value())
        {
            // Clear any stale dongle-related error from a previous
            // disconnect cycle — late subscribers should see "no
            // current problem" now that the dongle is talking.
            MessageBus::publish(MessageBus::DaemonError{});
            const auto& info = *introspection.dongleInfo;
            Logger::info("[ProtocolThread] dongle " + info.libraryVersion + " (lib type " +
                         std::to_string(static_cast<int>(info.libraryType)) + ", controller node " +
                         std::to_string(static_cast<int>(info.controllerNodeId)) + ")");
            state().controllerNodeId = info.controllerNodeId;
            // Bind the registry to this network *before* seeding from
            // init-data, so seeded entries land in the right home_id.
            NodeRegistry::setHomeId(info.homeId);
            MessageBus::publish(info);

            if (introspection.initData.has_value())
            {
                const auto& init = *introspection.initData;
                Logger::info("[ProtocolThread] init-data: " + std::to_string(init.nodeIds.size()) +
                             " node(s) included, chip type " + std::to_string(static_cast<int>(init.chipType)) +
                             " rev " + std::to_string(static_cast<int>(init.chipVersion)));
                for (const auto nodeId : init.nodeIds)
                {
                    NodeRegistry::seed(nodeId);
                }
                // Overlay each seeded skeleton with the device-class
                // triple captured from FUNC_ID_GET_NODE_PROTOCOL_INFO.
                // `updateDeviceClass` preserves any pre-existing
                // commandClasses (e.g. from a prior inclusion that
                // landed the full info), so this is safe to run
                // unconditionally.
                for (const auto& [nodeId, info] : introspection.nodeProtocolInfo)
                {
                    NodeRegistry::updateDeviceClass(
                        nodeId, info.basicDeviceType, info.genericDeviceType, info.specificDeviceType);
                }
                MessageBus::publish(init);
            }
        }

        clearRequests();
        state().activeFailedNodeRemoval.reset();
        state().activeRequestNodeInfo.reset();
        state().pendingLifelineNodeId.reset();
        runConnectedSession(port);
    }

    unsubscribeBus();
}

__attribute__((constructor(CONFIG_ZWAVE_PROTOCOL_PRIO))) auto startZWaveThread() -> void
{
    state().running = true;
    state().thread  = std::thread(zwaveCommunicationThread);
}
// Shutdown lives in ZwaveProtocolState's destructor (see comment there).
}  // namespace
