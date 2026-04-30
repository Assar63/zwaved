#include "../zwave-dongle/DeviceHandoff.hpp"
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
#include <iostream>
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

struct ZwaveProtocolState
{
    std::thread thread;
    std::atomic<bool> running{false};
};

auto state() -> ZwaveProtocolState&
{
    static ZwaveProtocolState instance;
    return instance;
}

auto handleIncomingFrame(HostApi::SessionTracker& tracker, const ZwaveDataFrame& frame) -> void
{
    const auto decoded = HostApi::decodeNodeCallback(frame);
    if (!decoded.has_value())
    {
        return;
    }
    HostApi::publishCallback(*decoded);
    if (HostApi::isTerminalStatus(decoded->commandId, decoded->status))
    {
        tracker.end();
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
        std::string const expected = DeviceHandoff::current();
        if (expected.empty())
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

    while (state().running)
    {
        auto path = DeviceHandoff::awaitDevicePath(state().running);
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

        HostApi::clear();
        HostApi::clearCallbacks();
        runConnectedSession(port);
    }
}

__attribute__((constructor(CONFIG_ZWAVE_PROTOCOL_PRIO))) auto startZWaveThread() -> void
{
    state().running = true;
    state().thread  = std::thread(zwaveCommunicationThread);
}

__attribute__((destructor(CONFIG_ZWAVE_PROTOCOL_PRIO))) auto stopZWaveThread() -> void
{
    state().running = false;
    DeviceHandoff::wakeAll();
    HostApi::wakeAll();
    HostApi::wakeAllCallbacks();
    if (state().thread.joinable())
    {
        state().thread.join();
    }
    std::cout << "Z-Wave communication thread shutdown complete\n";
}
}  // namespace