#include "../message-bus/MessageBus.hpp"
#include "../node-registry/NodeRegistry.hpp"
#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority
#include "FrameTransport.hpp"
#include "HostApi.hpp"
#include "HostApiSession.hpp"
#include "SerialPort.hpp"
#include "ZwaveDataFrame.hpp"
#include "application/Association.hpp"
#include "application/BinarySwitch.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
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

    // Bus subscription IDs, released on shutdown.
    MessageBus::SubscriptionId dongleSubscription{0};
    MessageBus::SubscriptionId addNodeSubscription{0};
    MessageBus::SubscriptionId removeNodeSubscription{0};
    MessageBus::SubscriptionId removeFailedNodeSubscription{0};
    MessageBus::SubscriptionId switchBinarySubscription{0};
    MessageBus::SubscriptionId setAssociationSubscription{0};
    MessageBus::SubscriptionId removeAssociationSubscription{0};
    MessageBus::SubscriptionId getAssociationSubscription{0};
    MessageBus::SubscriptionId getAssociationGroupingsSubscription{0};
    MessageBus::SubscriptionId behaviorConfigSubscription{0};

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
        std::cout << "Z-Wave communication thread shutdown complete\n";
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

auto onSetSwitchBinary(const MessageBus::SetSwitchBinaryCommand& cmd) -> void
{
    HostApi::SendDataRequest req{};
    req.nodeId     = cmd.nodeId;
    req.data       = BinarySwitch::encodeSet(cmd.turnOn);
    req.txOptions  = HostApi::TRANSMIT_OPTION_DEFAULT;
    req.callbackId = cmd.callbackId;
    pushRequest(req);
}

auto onSetAssociation(const MessageBus::SetAssociationCommand& cmd) -> void
{
    HostApi::SendDataRequest req{};
    req.nodeId     = cmd.nodeId;
    req.data       = Association::encodeSet(cmd.groupId, std::span<const std::uint8_t>(cmd.members));
    req.txOptions  = HostApi::TRANSMIT_OPTION_DEFAULT;
    req.callbackId = cmd.callbackId;
    pushRequest(req);
}

auto onRemoveAssociation(const MessageBus::RemoveAssociationCommand& cmd) -> void
{
    HostApi::SendDataRequest req{};
    req.nodeId     = cmd.nodeId;
    req.data       = Association::encodeRemove(cmd.groupId, std::span<const std::uint8_t>(cmd.members));
    req.txOptions  = HostApi::TRANSMIT_OPTION_DEFAULT;
    req.callbackId = cmd.callbackId;
    pushRequest(req);
}

auto onGetAssociation(const MessageBus::GetAssociationCommand& cmd) -> void
{
    HostApi::SendDataRequest req{};
    req.nodeId     = cmd.nodeId;
    req.data       = Association::encodeGet(cmd.groupId);
    req.txOptions  = HostApi::TRANSMIT_OPTION_DEFAULT;
    req.callbackId = cmd.callbackId;
    pushRequest(req);
}

auto onGetAssociationGroupings(const MessageBus::GetAssociationGroupingsCommand& cmd) -> void
{
    HostApi::SendDataRequest req{};
    req.nodeId     = cmd.nodeId;
    req.data       = Association::encodeGroupingsGet();
    req.txOptions  = HostApi::TRANSMIT_OPTION_DEFAULT;
    req.callbackId = cmd.callbackId;
    pushRequest(req);
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
};

/// Run the dongle's startup introspection: GET_VERSION (0x15) +
/// MEMORY_GET_ID (0x20) + SERIAL_API_GET_INIT_DATA (0x02). Uses a
/// short-lived FrameTransport with its own capturing handler so
/// RESPONSE frames land in local optionals without going through the
/// runtime callback path. GET_VERSION and MEMORY_GET_ID failures are
/// fatal (no DongleInfo); GET_INIT_DATA is best-effort — if it
/// times out or the dongle doesn't support the FUNC_ID, the result
/// just has no initData payload.
auto introspectDongle(SerialPort& port) -> IntrospectionResult
{
    using Clock = std::chrono::steady_clock;

    std::optional<HostApi::VersionResponse> version;
    std::optional<HostApi::MemoryIdResponse> identity;
    std::optional<HostApi::InitDataResponse> initData;

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

    IntrospectionResult result;

    if (!sendAndAwait(HostApi::CMD_GET_VERSION, version))
    {
        std::cerr << "[ProtocolThread] GET_VERSION introspection failed\n";
        return result;
    }
    if (!sendAndAwait(HostApi::CMD_MEMORY_GET_ID, identity))
    {
        std::cerr << "[ProtocolThread] MEMORY_GET_ID introspection failed\n";
        return result;
    }
    if (!sendAndAwait(HostApi::CMD_SERIAL_API_GET_INIT_DATA, initData))
    {
        std::cerr << "[ProtocolThread] SERIAL_API_GET_INIT_DATA introspection failed (continuing)\n";
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
    }
    return result;
}

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
                std::cout << "[ProtocolThread] auto-lifeline: SetAssociation node=" << static_cast<int>(*pendingNode)
                          << " group=1 controller=" << static_cast<int>(*controller) << '\n';
            }
            state().pendingLifelineNodeId.reset();
            tracker.end();
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
        [&]<typename T0>(T0 const& concrete) -> auto
        {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, HostApi::AddNodeRequest>)
            {
                ZwaveDataFrame const frame = HostApi::encodeAddNode(concrete);
                if (concrete.mode != HostApi::ADD_MODE_STOP && concrete.mode != HostApi::ADD_MODE_STOP_REPLICATION)
                {
                    tracker.begin(HostApi::CMD_ADD_NODE_TO_NETWORK, concrete.sessionId);
                }
                if (!transport.sendRequest(frame))
                {
                    std::cerr << "[ProtocolThread] AddNode send failed (session "
                              << static_cast<int>(concrete.sessionId) << ")\n";
                }
            }
            else if constexpr (std::is_same_v<T, HostApi::RemoveNodeRequest>)
            {
                ZwaveDataFrame const frame = HostApi::encodeRemoveNode(concrete);
                if (concrete.mode != HostApi::REMOVE_MODE_STOP)
                {
                    tracker.begin(HostApi::CMD_REMOVE_NODE_FROM_NETWORK, concrete.sessionId);
                }
                if (!transport.sendRequest(frame))
                {
                    std::cerr << "[ProtocolThread] RemoveNode send failed (session "
                              << static_cast<int>(concrete.sessionId) << ")\n";
                }
            }
            else if constexpr (std::is_same_v<T, HostApi::RemoveFailedNodeRequest>)
            {
                state().activeFailedNodeRemoval = concrete;
                ZwaveDataFrame const frame      = HostApi::encodeRemoveFailedNode(concrete);
                if (!transport.sendRequest(frame))
                {
                    std::cerr << "[ProtocolThread] RemoveFailedNode send failed (node "
                              << static_cast<int>(concrete.nodeId) << ", session "
                              << static_cast<int>(concrete.sessionId) << ")\n";
                    state().activeFailedNodeRemoval.reset();
                    MessageBus::publish(MessageBus::RemoveFailedNodeStatus{
                        .nodeId    = concrete.nodeId,
                        .sessionId = concrete.sessionId,
                        .phase     = MessageBus::RemoveFailedNodeStatus::PHASE_RESPONSE,
                        .status    = HostApi::RemoveFailedNodeResponse::STATUS_REMOVE_FAIL});
                }
            }
            else if constexpr (std::is_same_v<T, HostApi::SendDataRequest>)
            {
                ZwaveDataFrame const frame = HostApi::encodeSendData(concrete);
                if (!transport.sendRequest(frame))
                {
                    std::cerr << "[ProtocolThread] SendData send failed (node " << static_cast<int>(concrete.nodeId)
                              << ", callback " << static_cast<int>(concrete.callbackId) << ")\n";
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
            std::cout << "[ProtocolThread] device removed, closing serial port\n";
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

auto subscribeBus() -> void
{
    state().dongleSubscription     = MessageBus::subscribe<MessageBus::DongleStatus>(onDongleStatus);
    state().addNodeSubscription    = MessageBus::subscribe<MessageBus::AddNodeCommand>(onAddNodeCommand);
    state().removeNodeSubscription = MessageBus::subscribe<MessageBus::RemoveNodeCommand>(onRemoveNodeCommand);
    state().removeFailedNodeSubscription =
        MessageBus::subscribe<MessageBus::RemoveFailedNodeCommand>(onRemoveFailedNodeCommand);
    state().switchBinarySubscription   = MessageBus::subscribe<MessageBus::SetSwitchBinaryCommand>(onSetSwitchBinary);
    state().setAssociationSubscription = MessageBus::subscribe<MessageBus::SetAssociationCommand>(onSetAssociation);
    state().removeAssociationSubscription =
        MessageBus::subscribe<MessageBus::RemoveAssociationCommand>(onRemoveAssociation);
    state().getAssociationSubscription = MessageBus::subscribe<MessageBus::GetAssociationCommand>(onGetAssociation);
    state().getAssociationGroupingsSubscription =
        MessageBus::subscribe<MessageBus::GetAssociationGroupingsCommand>(onGetAssociationGroupings);
    state().behaviorConfigSubscription = MessageBus::subscribe<MessageBus::BehaviorConfig>(
        [](const MessageBus::BehaviorConfig& cfg) -> void
        { state().autoLifeline.store(cfg.autoLifeline, std::memory_order_relaxed); });
}

auto unsubscribeBus() -> void
{
    MessageBus::unsubscribe(state().behaviorConfigSubscription);
    MessageBus::unsubscribe(state().getAssociationGroupingsSubscription);
    MessageBus::unsubscribe(state().getAssociationSubscription);
    MessageBus::unsubscribe(state().removeAssociationSubscription);
    MessageBus::unsubscribe(state().setAssociationSubscription);
    MessageBus::unsubscribe(state().switchBinarySubscription);
    MessageBus::unsubscribe(state().removeFailedNodeSubscription);
    MessageBus::unsubscribe(state().removeNodeSubscription);
    MessageBus::unsubscribe(state().addNodeSubscription);
    MessageBus::unsubscribe(state().dongleSubscription);
    state().getAssociationGroupingsSubscription = 0;
    state().getAssociationSubscription          = 0;
    state().removeAssociationSubscription       = 0;
    state().setAssociationSubscription          = 0;
    state().switchBinarySubscription            = 0;
    state().removeFailedNodeSubscription        = 0;
    state().removeNodeSubscription              = 0;
    state().addNodeSubscription                 = 0;
    state().dongleSubscription                  = 0;
    state().behaviorConfigSubscription          = 0;
}

auto zwaveCommunicationThread() -> void
{
    prctl(PR_SET_NAME, "ZWaveProto", 0, 0, 0);  // NOLINT(misc-include-cleaner): PR_SET_NAME from <sys/prctl.h>

    subscribeBus();

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
            std::cerr << "[ProtocolThread] failed to open " << *path << "; backing off\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        if (auto introspection = introspectDongle(port); introspection.dongleInfo.has_value())
        {
            const auto& info = *introspection.dongleInfo;
            std::cout << "[ProtocolThread] dongle " << info.libraryVersion << " (lib type "
                      << static_cast<int>(info.libraryType) << ", controller node "
                      << static_cast<int>(info.controllerNodeId) << ")\n";
            state().controllerNodeId = info.controllerNodeId;
            // Bind the registry to this network *before* seeding from
            // init-data, so seeded entries land in the right home_id.
            NodeRegistry::setHomeId(info.homeId);
            MessageBus::publish(info);

            if (introspection.initData.has_value())
            {
                const auto& init = *introspection.initData;
                std::cout << "[ProtocolThread] init-data: " << init.nodeIds.size() << " node(s) included, chip type "
                          << static_cast<int>(init.chipType) << " rev " << static_cast<int>(init.chipVersion) << '\n';
                for (const auto nodeId : init.nodeIds)
                {
                    NodeRegistry::seed(nodeId);
                }
                MessageBus::publish(init);
            }
        }

        clearRequests();
        state().activeFailedNodeRemoval.reset();
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
