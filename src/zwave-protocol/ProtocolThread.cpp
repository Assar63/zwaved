#include "../message-bus/MessageBus.hpp"
#include "../node-registry/NodeRegistry.hpp"
#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority
#include "FrameTransport.hpp"
#include "HostApi.hpp"
#include "HostApiCallbackDispatcher.hpp"
#include "HostApiRequestQueue.hpp"
#include "HostApiSession.hpp"
#include "SerialPort.hpp"
#include "ZwaveDataFrame.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>

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

    MessageBus::SubscriptionId dongleSubscription{0};
};

auto state() -> ZwaveProtocolState&
{
    static ZwaveProtocolState instance;
    return instance;
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

/// Run the dongle's startup introspection: GET_VERSION (0x15) +
/// MEMORY_GET_ID (0x20). Uses a short-lived FrameTransport with its
/// own capturing handler so RESPONSE frames land in local optionals
/// without going through the runtime callback path. Returns
/// std::nullopt if either request times out or fails to send.
auto introspectDongle(SerialPort& port) -> std::optional<MessageBus::DongleInfo>
{
    using Clock = std::chrono::steady_clock;

    std::optional<HostApi::VersionResponse> version;
    std::optional<HostApi::MemoryIdResponse> identity;

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

    if (!sendAndAwait(HostApi::CMD_GET_VERSION, version))
    {
        std::cerr << "[ProtocolThread] GET_VERSION introspection failed\n";
        return std::nullopt;
    }
    if (!sendAndAwait(HostApi::CMD_MEMORY_GET_ID, identity))
    {
        std::cerr << "[ProtocolThread] MEMORY_GET_ID introspection failed\n";
        return std::nullopt;
    }
    // Belt-and-braces: sendAndAwait already returns false unless the slot was
    // populated, but the static analyzer can't trace that across the helper.
    if (!version.has_value() || !identity.has_value())
    {
        return std::nullopt;
    }

    MessageBus::DongleInfo info;
    info.libraryVersion = version->version;
    info.libraryType    = version->libraryType;
    info.homeId.assign(identity->homeId.begin(), identity->homeId.end());
    info.controllerNodeId = identity->controllerNodeId;
    return info;
}

auto handleIncomingFrame(HostApi::SessionTracker& tracker, const ZwaveDataFrame& frame) -> void
{
    if (const auto sendDataCb = HostApi::decodeSendDataCallback(frame); sendDataCb.has_value())
    {
        HostApi::publishCallback(*sendDataCb);
        return;
    }
    if (const auto appCmd = HostApi::decodeApplicationCommand(frame); appCmd.has_value())
    {
        MessageBus::publish(MessageBus::ApplicationCommand{
            .rxStatus = appCmd->rxStatus, .sourceNodeId = appCmd->sourceNodeId, .ccData = appCmd->ccData});
        return;
    }
    if (const auto nodeCb = HostApi::decodeNodeCallback(frame); nodeCb.has_value())
    {
        HostApi::publishCallback(*nodeCb);

        const bool hasNodePayload =
            nodeCb->nodeId != 0 && (nodeCb->status == STATUS_PROTOCOL_DONE || nodeCb->status == STATUS_COMPLETED);
        if (hasNodePayload)
        {
            if (nodeCb->commandId == HostApi::CMD_ADD_NODE_TO_NETWORK)
            {
                NodeRegistry::add({.nodeId         = static_cast<std::uint8_t>(nodeCb->nodeId),
                                   .basicType      = nodeCb->basicDeviceType,
                                   .genericType    = nodeCb->genericDeviceType,
                                   .specificType   = nodeCb->specificDeviceType,
                                   .commandClasses = nodeCb->commandClasses});
            }
            else if (nodeCb->commandId == HostApi::CMD_REMOVE_NODE_FROM_NETWORK)
            {
                NodeRegistry::remove(static_cast<std::uint8_t>(nodeCb->nodeId));
            }
        }

        if (HostApi::isTerminalStatus(nodeCb->commandId, nodeCb->status))
        {
            tracker.end();
        }
    }
}

auto dispatchRequest(FrameTransport& transport,
                     HostApi::SessionTracker& tracker,
                     const HostApi::Request& request) -> void
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
            else if constexpr (std::is_same_v<T, HostApi::SendDataRequest>)
            {
                ZwaveDataFrame const frame = HostApi::encodeSendData(concrete);
                if (!transport.sendRequest(frame))
                {
                    std::cerr << "[ProtocolThread] SendData send failed (node " << static_cast<int>(concrete.nodeId)
                              << ", callback " << static_cast<int>(concrete.callbackId) << ")\n";
                    // Synthesize a failure callback so the external API gets
                    // notified instead of waiting indefinitely for a response.
                    HostApi::publishCallback(HostApi::SendDataCallback{.callbackId = concrete.callbackId,
                                                                       .txStatus   = HostApi::TRANSMIT_COMPLETE_FAIL});
                }
            }
        },
        request);
}

auto runConnectedSession(SerialPort& port) -> void
{
    HostApi::SessionTracker tracker;
    FrameTransport transport(&port,
                             [&tracker](const ZwaveDataFrame& frame) -> void { handleIncomingFrame(tracker, frame); });

    while (state().running && port.isOpen())
    {
        if (!isPathSet())
        {
            std::cout << "[ProtocolThread] device removed, closing serial port\n";
            port.close();
            break;
        }

        auto request = HostApi::popRequest(state().running, REQUEST_WAIT_TIMEOUT_MS);
        if (request.has_value())
        {
            dispatchRequest(transport, tracker, *request);
        }
        else
        {
            transport.pumpOnce(IDLE_PUMP_TIMEOUT_MS);
        }
    }
}

auto zwaveCommunicationThread() -> void
{
    prctl(PR_SET_NAME, "ZWaveProto", 0, 0, 0);  // NOLINT(misc-include-cleaner): PR_SET_NAME from <sys/prctl.h>

    state().dongleSubscription = MessageBus::subscribe(onDongleStatus);

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

        if (auto info = introspectDongle(port); info.has_value())
        {
            std::cout << "[ProtocolThread] dongle " << info->libraryVersion << " (lib type "
                      << static_cast<int>(info->libraryType) << ", controller node "
                      << static_cast<int>(info->controllerNodeId) << ")\n";
            MessageBus::publish(*info);
        }

        HostApi::clear();
        HostApi::clearCallbacks();
        runConnectedSession(port);
    }

    MessageBus::unsubscribe(state().dongleSubscription);
    state().dongleSubscription = 0;
}

__attribute__((constructor(CONFIG_ZWAVE_PROTOCOL_PRIO))) auto startZWaveThread() -> void
{
    state().running = true;
    state().thread  = std::thread(zwaveCommunicationThread);
}

__attribute__((destructor(CONFIG_ZWAVE_PROTOCOL_PRIO))) auto stopZWaveThread() -> void
{
    state().running = false;
    state().pathCv.notify_all();
    HostApi::wakeAll();
    HostApi::wakeAllCallbacks();
    if (state().thread.joinable())
    {
        state().thread.join();
    }
    std::cout << "Z-Wave communication thread shutdown complete\n";
}
}  // namespace
