#include "DBusBackend.hpp"

#include "../message-bus/MessageBus.hpp"
#include "../node-registry/NodeRegistry.hpp"
#include "../zwave-protocol/BinarySwitch.hpp"
#include "../zwave-protocol/HostApi.hpp"
#include "../zwave-protocol/HostApiCallbackDispatcher.hpp"
#include "../zwave-protocol/HostApiRequestQueue.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IObject.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>

namespace
{
constexpr const char* BUS_NAME    = "com.tiunda.ZWaved";
constexpr const char* OBJECT_PATH = "/com/tiunda/ZWaved";
constexpr const char* IFACE_NAME  = "com.tiunda.ZWaved1";

constexpr const char* SIGNAL_INCLUSION_STATUS     = "NodeInclusionStatus";
constexpr const char* SIGNAL_EXCLUSION_STATUS     = "NodeExclusionStatus";
constexpr const char* SIGNAL_DONGLE_STATUS        = "DongleStatus";
constexpr const char* SIGNAL_SEND_DATA_STATUS     = "SendDataStatus";
constexpr const char* SIGNAL_APPLICATION_COMMAND  = "ApplicationCommand";
constexpr const char* SIGNAL_SWITCH_BINARY_REPORT = "SwitchBinaryReport";

constexpr int CALLBACK_POLL_MS = 200;

constexpr uint8_t FLAG_POWER_BIT    = 7;
constexpr uint8_t FLAG_NWI_BIT      = 6;
constexpr uint8_t FLAG_PROTOCOL_BIT = 5;
constexpr uint8_t FLAG_SFLND_BIT    = 4;
constexpr uint8_t FLAG_NWE_BIT      = 6;

[[nodiscard]] auto bitSet(const uint8_t value, const uint8_t bit) -> bool
{
    return (value & static_cast<uint8_t>(1U << bit)) != 0U;
}

[[nodiscard]] auto homeIdFromVector(const std::vector<uint8_t>& bytes) -> std::array<uint8_t, 4>
{
    std::array<uint8_t, 4> result{};
    for (std::size_t i = 0; i < bytes.size() && i < result.size(); ++i)
    {
        result.at(i) = bytes.at(i);
    }
    return result;
}

auto emitApplicationCommand(sdbus::IObject& obj, const MessageBus::ApplicationCommand& event) -> void
{
    try
    {
        auto raw = obj.createSignal(IFACE_NAME, SIGNAL_APPLICATION_COMMAND);
        raw << event.rxStatus;
        raw << event.sourceNodeId;
        raw << event.ccData;
        obj.emitSignal(raw);

        if (const auto report = BinarySwitch::decodeReport(std::span<const uint8_t>(event.ccData)); report.has_value())
        {
            auto typed = obj.createSignal(IFACE_NAME, SIGNAL_SWITCH_BINARY_REPORT);
            typed << event.sourceNodeId;
            typed << static_cast<uint8_t>(report->state);
            obj.emitSignal(typed);
        }
    }
    catch (const sdbus::Error& err)
    {
        std::cerr << "[DBusBackend] failed to emit ApplicationCommand: " << err.what() << '\n';
    }
}
}  // namespace

namespace ExternalApi
{
struct DBusBackend::Impl
{
    std::unique_ptr<sdbus::IConnection> connection;
    std::unique_ptr<sdbus::IObject> object;
    std::atomic<bool> connected{false};
    MessageBus::SubscriptionId dongleSubscription{0};
    MessageBus::SubscriptionId applicationCommandSubscription{0};
};

DBusBackend::DBusBackend()
    : impl(std::make_unique<Impl>())
{
}

DBusBackend::~DBusBackend()
{
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall): static dispatch of own stop() is intended for cleanup
    stop();
}

auto DBusBackend::run(const std::atomic<bool>& running) -> void
{
    try
    {
        impl->connection = sdbus::createSystemBusConnection(BUS_NAME);
    }
    catch (const sdbus::Error& err)
    {
        std::cerr << "[DBusBackend] failed to acquire system bus name " << BUS_NAME << ": " << err.what() << '\n';
        return;
    }

    impl->object    = sdbus::createObject(*impl->connection, OBJECT_PATH);
    impl->connected = true;

    auto& obj = *impl->object;

    obj.registerMethod("AddNode")
        .onInterface(IFACE_NAME)
        .implementedAs(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
            [](const uint8_t& mode,
               const uint8_t& flags,
               const uint8_t& sessionId,
               const std::vector<uint8_t>& nwiHomeId,
               const std::vector<uint8_t>& authHomeId)
            {
                HostApi::AddNodeRequest req{};
                req.mode              = mode;
                req.power             = bitSet(flags, FLAG_POWER_BIT);
                req.nwi               = bitSet(flags, FLAG_NWI_BIT);
                req.protocolLongRange = bitSet(flags, FLAG_PROTOCOL_BIT);
                req.skipFlNeighbors   = bitSet(flags, FLAG_SFLND_BIT);
                req.sessionId         = sessionId;
                req.includeHomeIds    = !nwiHomeId.empty() || !authHomeId.empty();
                req.nwiHomeId         = homeIdFromVector(nwiHomeId);
                req.authHomeId        = homeIdFromVector(authHomeId);
                HostApi::pushRequest(req);
            });

    obj.registerMethod("StopAddNode")
        .onInterface(IFACE_NAME)
        .implementedAs(
            [](const uint8_t& sessionId)
            {
                HostApi::AddNodeRequest req{};
                req.mode      = HostApi::ADD_MODE_STOP;
                req.sessionId = sessionId;
                HostApi::pushRequest(req);
            });

    obj.registerMethod("RemoveNode")
        .onInterface(IFACE_NAME)
        .implementedAs(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
            [](const uint8_t& mode, const uint8_t& flags, const uint8_t& sessionId)
            {
                HostApi::RemoveNodeRequest req{};
                req.mode      = mode;
                req.power     = bitSet(flags, FLAG_POWER_BIT);
                req.nwe       = bitSet(flags, FLAG_NWE_BIT);
                req.sessionId = sessionId;
                HostApi::pushRequest(req);
            });

    obj.registerMethod("StopRemoveNode")
        .onInterface(IFACE_NAME)
        .implementedAs(
            [](const uint8_t& sessionId)
            {
                HostApi::RemoveNodeRequest req{};
                req.mode      = HostApi::REMOVE_MODE_STOP;
                req.sessionId = sessionId;
                HostApi::pushRequest(req);
            });

    obj.registerMethod("SetSwitchBinary")
        .onInterface(IFACE_NAME)
        .implementedAs(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
            [](const uint8_t& nodeId, const bool& turnOn, const uint8_t& callbackId)
            {
                HostApi::SendDataRequest req{};
                req.nodeId     = nodeId;
                req.data       = BinarySwitch::encodeSet(turnOn);
                req.txOptions  = HostApi::TRANSMIT_OPTION_DEFAULT;
                req.callbackId = callbackId;
                HostApi::pushRequest(req);
            });

    using NodeTuple = sdbus::Struct<uint8_t, uint8_t, uint8_t, uint8_t, std::vector<uint8_t>>;
    obj.registerMethod("GetNodes")
        .onInterface(IFACE_NAME)
        .implementedAs(
            []() -> std::vector<NodeTuple>
            {
                std::vector<NodeTuple> result;
                for (const auto& info : NodeRegistry::snapshot())
                {
                    result.emplace_back(
                        info.nodeId, info.basicType, info.genericType, info.specificType, info.commandClasses);
                }
                return result;
            });

    obj.registerSignal(SIGNAL_INCLUSION_STATUS)
        .onInterface(IFACE_NAME)
        .withParameters<uint8_t, uint8_t, uint16_t, uint8_t, uint8_t, uint8_t, std::vector<uint8_t>>(
            "sessionId", "status", "nodeId", "basicType", "genericType", "specificType", "commandClasses");

    obj.registerSignal(SIGNAL_EXCLUSION_STATUS)
        .onInterface(IFACE_NAME)
        .withParameters<uint8_t, uint8_t, uint16_t, uint8_t, uint8_t, uint8_t, std::vector<uint8_t>>(
            "sessionId", "status", "nodeId", "basicType", "genericType", "specificType", "commandClasses");

    obj.registerSignal(SIGNAL_DONGLE_STATUS)
        .onInterface(IFACE_NAME)
        .withParameters<bool, std::string>("connected", "ttyPath");

    obj.registerSignal(SIGNAL_SEND_DATA_STATUS)
        .onInterface(IFACE_NAME)
        .withParameters<uint8_t, uint8_t>("callbackId", "txStatus");

    obj.registerSignal(SIGNAL_APPLICATION_COMMAND)
        .onInterface(IFACE_NAME)
        .withParameters<uint8_t, uint8_t, std::vector<uint8_t>>("rxStatus", "sourceNodeId", "ccData");

    obj.registerSignal(SIGNAL_SWITCH_BINARY_REPORT)
        .onInterface(IFACE_NAME)
        .withParameters<uint8_t, uint8_t>("sourceNodeId", "state");

    obj.finishRegistration();

    impl->dongleSubscription = MessageBus::subscribe(
        [this](const MessageBus::DongleStatus& status)
        {
            if (!impl || !impl->connected.load() || !impl->object)
            {
                return;
            }
            try
            {
                auto signal = impl->object->createSignal(IFACE_NAME, SIGNAL_DONGLE_STATUS);
                signal << status.connected;
                signal << status.ttyPath;
                impl->object->emitSignal(signal);
            }
            catch (const sdbus::Error& err)
            {
                std::cerr << "[DBusBackend] failed to emit DongleStatus: " << err.what() << '\n';
            }
        });

    impl->applicationCommandSubscription = MessageBus::subscribe(
        [this](const MessageBus::ApplicationCommand& event)
        {
            if (!impl || !impl->connected.load() || !impl->object)
            {
                return;
            }
            emitApplicationCommand(*impl->object, event);
        });

    impl->connection->enterEventLoopAsync();
    std::cout << "[DBusBackend] listening on system bus as " << BUS_NAME << '\n';

    while (running.load())
    {
        auto callback = HostApi::popCallback(running, CALLBACK_POLL_MS);
        if (!callback.has_value())
        {
            continue;
        }
        try
        {
            std::visit(
                [&obj](auto const& concrete)
                {
                    using T = std::decay_t<decltype(concrete)>;
                    if constexpr (std::is_same_v<T, HostApi::NodeStatusCallback>)
                    {
                        const char* signalName = concrete.commandId == HostApi::CMD_REMOVE_NODE_FROM_NETWORK
                                                     ? SIGNAL_EXCLUSION_STATUS
                                                     : SIGNAL_INCLUSION_STATUS;
                        auto signal            = obj.createSignal(IFACE_NAME, signalName);
                        signal << concrete.sessionId;
                        signal << concrete.status;
                        signal << concrete.nodeId;
                        signal << concrete.basicDeviceType;
                        signal << concrete.genericDeviceType;
                        signal << concrete.specificDeviceType;
                        signal << concrete.commandClasses;
                        obj.emitSignal(signal);
                    }
                    else if constexpr (std::is_same_v<T, HostApi::SendDataCallback>)
                    {
                        auto signal = obj.createSignal(IFACE_NAME, SIGNAL_SEND_DATA_STATUS);
                        signal << concrete.callbackId;
                        signal << concrete.txStatus;
                        obj.emitSignal(signal);
                    }
                },
                *callback);
        }
        catch (const sdbus::Error& err)
        {
            std::cerr << "[DBusBackend] failed to emit signal: " << err.what() << '\n';
        }
    }

    impl->connection->leaveEventLoop();
    impl->connected = false;
}

auto DBusBackend::stop() -> void
{
    if (impl && impl->applicationCommandSubscription != 0)
    {
        MessageBus::unsubscribe(impl->applicationCommandSubscription);
        impl->applicationCommandSubscription = 0;
    }
    if (impl && impl->dongleSubscription != 0)
    {
        MessageBus::unsubscribe(impl->dongleSubscription);
        impl->dongleSubscription = 0;
    }
    if (impl && impl->connected.load() && impl->connection)
    {
        try
        {
            impl->connection->leaveEventLoop();
        }
        catch (const sdbus::Error& err)
        {
            std::cerr << "[DBusBackend] leaveEventLoop: " << err.what() << '\n';
        }
        impl->connected = false;
    }
    HostApi::wakeAllCallbacks();
}
}  // namespace ExternalApi
