#include "DBusBackend.hpp"

#include "../message-bus/MessageBus.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

constexpr const char* SIGNAL_INCLUSION_STATUS    = "NodeInclusionStatus";
constexpr const char* SIGNAL_EXCLUSION_STATUS    = "NodeExclusionStatus";
constexpr const char* SIGNAL_DONGLE_STATUS       = "DongleStatus";
constexpr const char* SIGNAL_DONGLE_INFO         = "DongleInfo";
constexpr const char* SIGNAL_INIT_DATA           = "InitData";
constexpr const char* SIGNAL_SEND_DATA_STATUS    = "SendDataStatus";
constexpr const char* SIGNAL_APPLICATION_COMMAND = "ApplicationCommand";

constexpr int IDLE_POLL_MS = 200;

constexpr std::uint8_t FLAG_POWER_BIT    = 7;
constexpr std::uint8_t FLAG_NWI_BIT      = 6;
constexpr std::uint8_t FLAG_PROTOCOL_BIT = 5;
constexpr std::uint8_t FLAG_SFLND_BIT    = 4;
constexpr std::uint8_t FLAG_NWE_BIT      = 6;

[[nodiscard]] auto bitSet(const std::uint8_t value, const std::uint8_t bit) -> bool
{
    return (value & static_cast<std::uint8_t>(1U << bit)) != 0U;
}

[[nodiscard]] auto homeIdFromVector(const std::vector<std::uint8_t>& bytes) -> std::array<std::uint8_t, 4>
{
    std::array<std::uint8_t, 4> result{};
    for (std::size_t i = 0; i < bytes.size() && i < result.size(); ++i)
    {
        result.at(i) = bytes.at(i);
    }
    return result;
}
}  // namespace

namespace ExternalApi
{
struct DBusBackend::Impl
{
    std::unique_ptr<sdbus::IConnection> connection;
    std::unique_ptr<sdbus::IObject> object;
    std::atomic<bool> connected{false};

    MessageBus::SubscriptionId dongleStatusSub{0};
    MessageBus::SubscriptionId dongleInfoSub{0};
    MessageBus::SubscriptionId initDataSub{0};
    MessageBus::SubscriptionId applicationCommandSub{0};
    MessageBus::SubscriptionId nodeListSub{0};
    MessageBus::SubscriptionId inclusionSub{0};
    MessageBus::SubscriptionId exclusionSub{0};
    MessageBus::SubscriptionId sendDataSub{0};

    // Cached state replayed to D-Bus method callers (GetDongleInfo,
    // GetInitData, GetNodes). Each is fed by a retained MessageBus
    // event so a late D-Bus client gets the latest value without the
    // backend reaching into the producing module.
    std::mutex stateMutex;
    MessageBus::DongleInfo lastDongleInfo;
    MessageBus::InitData lastInitData;
    std::vector<MessageBus::NodeInfo> lastNodes;
};

DBusBackend::DBusBackend()
    : impl(std::make_unique<Impl>())
{
}

DBusBackend::~DBusBackend()
{
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall): static dispatch of own stop() is intended for cleanup
    DBusBackend::stop();
}

// NOLINTBEGIN(readability-function-cognitive-complexity): flat D-Bus method/signal registration list
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
            [](const std::uint8_t& mode,
               const std::uint8_t& flags,
               const std::uint8_t& sessionId,
               const std::vector<std::uint8_t>& nwiHomeId,
               const std::vector<std::uint8_t>& authHomeId) -> void
            {
                MessageBus::publish(
                    MessageBus::AddNodeCommand{.mode              = mode,
                                               .power             = bitSet(flags, FLAG_POWER_BIT),
                                               .nwi               = bitSet(flags, FLAG_NWI_BIT),
                                               .protocolLongRange = bitSet(flags, FLAG_PROTOCOL_BIT),
                                               .skipFlNeighbors   = bitSet(flags, FLAG_SFLND_BIT),
                                               .sessionId         = sessionId,
                                               .includeHomeIds    = !nwiHomeId.empty() || !authHomeId.empty(),
                                               .nwiHomeId         = homeIdFromVector(nwiHomeId),
                                               .authHomeId        = homeIdFromVector(authHomeId)});
            });

    obj.registerMethod("StopAddNode")
        .onInterface(IFACE_NAME)
        .implementedAs(
            [](const std::uint8_t& sessionId) -> void
            {
                MessageBus::publish(
                    MessageBus::AddNodeCommand{.mode = MessageBus::AddNodeCommand::MODE_STOP, .sessionId = sessionId});
            });

    obj.registerMethod("RemoveNode")
        .onInterface(IFACE_NAME)
        .implementedAs(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
            [](const std::uint8_t& mode, const std::uint8_t& flags, const std::uint8_t& sessionId) -> void
            {
                MessageBus::publish(MessageBus::RemoveNodeCommand{.mode      = mode,
                                                                  .power     = bitSet(flags, FLAG_POWER_BIT),
                                                                  .nwe       = bitSet(flags, FLAG_NWE_BIT),
                                                                  .sessionId = sessionId});
            });

    obj.registerMethod("StopRemoveNode")
        .onInterface(IFACE_NAME)
        .implementedAs(
            [](const std::uint8_t& sessionId) -> void
            {
                MessageBus::publish(MessageBus::RemoveNodeCommand{.mode      = MessageBus::RemoveNodeCommand::MODE_STOP,
                                                                  .sessionId = sessionId});
            });

    obj.registerMethod("SetSwitchBinary")
        .onInterface(IFACE_NAME)
        .implementedAs(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
            [](const std::uint8_t& nodeId, const bool& turnOn, const std::uint8_t& callbackId) -> void
            {
                MessageBus::publish(
                    MessageBus::SetSwitchBinaryCommand{.nodeId = nodeId, .turnOn = turnOn, .callbackId = callbackId});
            });

    obj.registerMethod("SetAssociation")
        .onInterface(IFACE_NAME)
        .implementedAs(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
            [](const std::uint8_t& nodeId,
               const std::uint8_t& groupId,
               const std::vector<std::uint8_t>& members,
               const std::uint8_t& callbackId) -> void
            {
                MessageBus::publish(MessageBus::SetAssociationCommand{
                    .nodeId = nodeId, .groupId = groupId, .members = members, .callbackId = callbackId});
            });

    obj.registerMethod("RemoveAssociation")
        .onInterface(IFACE_NAME)
        .implementedAs(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
            [](const std::uint8_t& nodeId,
               const std::uint8_t& groupId,
               const std::vector<std::uint8_t>& members,
               const std::uint8_t& callbackId) -> void
            {
                MessageBus::publish(MessageBus::RemoveAssociationCommand{
                    .nodeId = nodeId, .groupId = groupId, .members = members, .callbackId = callbackId});
            });

    obj.registerMethod("GetAssociation")
        .onInterface(IFACE_NAME)
        .implementedAs(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
            [](const std::uint8_t& nodeId, const std::uint8_t& groupId, const std::uint8_t& callbackId) -> void
            {
                MessageBus::publish(
                    MessageBus::GetAssociationCommand{.nodeId = nodeId, .groupId = groupId, .callbackId = callbackId});
            });

    obj.registerMethod("GetAssociationGroupings")
        .onInterface(IFACE_NAME)
        .implementedAs(
            [](const std::uint8_t& nodeId, const std::uint8_t& callbackId) -> void {
                MessageBus::publish(
                    MessageBus::GetAssociationGroupingsCommand{.nodeId = nodeId, .callbackId = callbackId});
            });

    using NodeTuple = sdbus::Struct<uint8_t, uint8_t, uint8_t, uint8_t, std::vector<uint8_t>>;
    obj.registerMethod("GetNodes")
        .onInterface(IFACE_NAME)
        .implementedAs(
            [this]() -> std::vector<NodeTuple>
            {
                std::vector<NodeTuple> result;
                std::scoped_lock const lock(impl->stateMutex);
                result.reserve(impl->lastNodes.size());
                for (const auto& info : impl->lastNodes)
                {
                    result.emplace_back(
                        info.nodeId, info.basicType, info.genericType, info.specificType, info.commandClasses);
                }
                return result;
            });

    using DongleInfoTuple = sdbus::Struct<std::string, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t>;
    obj.registerMethod("GetDongleInfo")
        .onInterface(IFACE_NAME)
        .implementedAs(
            [this]() -> DongleInfoTuple
            {
                std::scoped_lock const lock(impl->stateMutex);
                return DongleInfoTuple{impl->lastDongleInfo.libraryVersion,
                                       impl->lastDongleInfo.libraryType,
                                       impl->lastDongleInfo.homeId,
                                       impl->lastDongleInfo.controllerNodeId};
            });

    using InitDataTuple =
        sdbus::Struct<std::uint8_t, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t, std::uint8_t>;
    obj.registerMethod("GetInitData")
        .onInterface(IFACE_NAME)
        .implementedAs(
            [this]() -> InitDataTuple
            {
                std::scoped_lock const lock(impl->stateMutex);
                return InitDataTuple{impl->lastInitData.serialApiVersion,
                                     impl->lastInitData.capabilities,
                                     impl->lastInitData.nodeIds,
                                     impl->lastInitData.chipType,
                                     impl->lastInitData.chipVersion};
            });

    obj.registerSignal(SIGNAL_INCLUSION_STATUS)
        .onInterface(IFACE_NAME)
        .withParameters<std::uint8_t,
                        std::uint8_t,
                        std::uint16_t,
                        std::uint8_t,
                        std::uint8_t,
                        std::uint8_t,
                        std::vector<std::uint8_t>>(
            "sessionId", "status", "nodeId", "basicType", "genericType", "specificType", "commandClasses");

    obj.registerSignal(SIGNAL_EXCLUSION_STATUS)
        .onInterface(IFACE_NAME)
        .withParameters<std::uint8_t,
                        std::uint8_t,
                        std::uint16_t,
                        std::uint8_t,
                        std::uint8_t,
                        std::uint8_t,
                        std::vector<std::uint8_t>>(
            "sessionId", "status", "nodeId", "basicType", "genericType", "specificType", "commandClasses");

    obj.registerSignal(SIGNAL_DONGLE_STATUS)
        .onInterface(IFACE_NAME)
        .withParameters<bool, std::string>("connected", "ttyPath");

    obj.registerSignal(SIGNAL_DONGLE_INFO)
        .onInterface(IFACE_NAME)
        .withParameters<std::string, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t>(
            "libraryVersion", "libraryType", "homeId", "controllerNodeId");

    obj.registerSignal(SIGNAL_INIT_DATA)
        .onInterface(IFACE_NAME)
        .withParameters<std::uint8_t, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t, std::uint8_t>(
            "serialApiVersion", "capabilities", "nodeIds", "chipType", "chipVersion");

    obj.registerSignal(SIGNAL_SEND_DATA_STATUS)
        .onInterface(IFACE_NAME)
        .withParameters<std::uint8_t, std::uint8_t>("callbackId", "txStatus");

    obj.registerSignal(SIGNAL_APPLICATION_COMMAND)
        .onInterface(IFACE_NAME)
        .withParameters<std::uint8_t, std::uint8_t, std::vector<std::uint8_t>>("rxStatus", "sourceNodeId", "ccData");

    obj.finishRegistration();

    impl->dongleStatusSub = MessageBus::subscribe<MessageBus::DongleStatus>(
        [this](const MessageBus::DongleStatus& status) -> void
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

    impl->dongleInfoSub = MessageBus::subscribe<MessageBus::DongleInfo>(
        [this](const MessageBus::DongleInfo& info) -> void
        {
            {
                std::scoped_lock const lock(impl->stateMutex);
                impl->lastDongleInfo = info;
            }
            if (!impl || !impl->connected.load() || !impl->object)
            {
                return;
            }
            try
            {
                auto signal = impl->object->createSignal(IFACE_NAME, SIGNAL_DONGLE_INFO);
                signal << info.libraryVersion;
                signal << info.libraryType;
                signal << info.homeId;
                signal << info.controllerNodeId;
                impl->object->emitSignal(signal);
            }
            catch (const sdbus::Error& err)
            {
                std::cerr << "[DBusBackend] failed to emit DongleInfo: " << err.what() << '\n';
            }
        });

    impl->initDataSub = MessageBus::subscribe<MessageBus::InitData>(
        [this](const MessageBus::InitData& info) -> void
        {
            {
                std::scoped_lock const lock(impl->stateMutex);
                impl->lastInitData = info;
            }
            if (!impl || !impl->connected.load() || !impl->object)
            {
                return;
            }
            try
            {
                auto signal = impl->object->createSignal(IFACE_NAME, SIGNAL_INIT_DATA);
                signal << info.serialApiVersion;
                signal << info.capabilities;
                signal << info.nodeIds;
                signal << info.chipType;
                signal << info.chipVersion;
                impl->object->emitSignal(signal);
            }
            catch (const sdbus::Error& err)
            {
                std::cerr << "[DBusBackend] failed to emit InitData: " << err.what() << '\n';
            }
        });

    impl->nodeListSub = MessageBus::subscribe<MessageBus::NodeListChanged>(
        [this](const MessageBus::NodeListChanged& event) -> void
        {
            std::scoped_lock const lock(impl->stateMutex);
            impl->lastNodes = event.nodes;
        });

    impl->applicationCommandSub = MessageBus::subscribe<MessageBus::ApplicationCommand>(
        [this](const MessageBus::ApplicationCommand& event) -> void
        {
            if (!impl || !impl->connected.load() || !impl->object)
            {
                return;
            }
            try
            {
                auto signal = impl->object->createSignal(IFACE_NAME, SIGNAL_APPLICATION_COMMAND);
                signal << event.rxStatus;
                signal << event.sourceNodeId;
                signal << event.ccData;
                impl->object->emitSignal(signal);
            }
            catch (const sdbus::Error& err)
            {
                std::cerr << "[DBusBackend] failed to emit ApplicationCommand: " << err.what() << '\n';
            }
        });

    impl->inclusionSub = MessageBus::subscribe<MessageBus::NodeInclusionStatus>(
        [this](const MessageBus::NodeInclusionStatus& event) -> void
        {
            if (!impl || !impl->connected.load() || !impl->object)
            {
                return;
            }
            try
            {
                auto signal = impl->object->createSignal(IFACE_NAME, SIGNAL_INCLUSION_STATUS);
                signal << event.sessionId;
                signal << event.status;
                signal << event.nodeId;
                signal << event.basicDeviceType;
                signal << event.genericDeviceType;
                signal << event.specificDeviceType;
                signal << event.commandClasses;
                impl->object->emitSignal(signal);
            }
            catch (const sdbus::Error& err)
            {
                std::cerr << "[DBusBackend] failed to emit NodeInclusionStatus: " << err.what() << '\n';
            }
        });

    impl->exclusionSub = MessageBus::subscribe<MessageBus::NodeExclusionStatus>(
        [this](const MessageBus::NodeExclusionStatus& event) -> void
        {
            if (!impl || !impl->connected.load() || !impl->object)
            {
                return;
            }
            try
            {
                auto signal = impl->object->createSignal(IFACE_NAME, SIGNAL_EXCLUSION_STATUS);
                signal << event.sessionId;
                signal << event.status;
                signal << event.nodeId;
                signal << event.basicDeviceType;
                signal << event.genericDeviceType;
                signal << event.specificDeviceType;
                signal << event.commandClasses;
                impl->object->emitSignal(signal);
            }
            catch (const sdbus::Error& err)
            {
                std::cerr << "[DBusBackend] failed to emit NodeExclusionStatus: " << err.what() << '\n';
            }
        });

    impl->sendDataSub = MessageBus::subscribe<MessageBus::SendDataCallback>(
        [this](const MessageBus::SendDataCallback& event) -> void
        {
            if (!impl || !impl->connected.load() || !impl->object)
            {
                return;
            }
            try
            {
                auto signal = impl->object->createSignal(IFACE_NAME, SIGNAL_SEND_DATA_STATUS);
                signal << event.callbackId;
                signal << event.txStatus;
                impl->object->emitSignal(signal);
            }
            catch (const sdbus::Error& err)
            {
                std::cerr << "[DBusBackend] failed to emit SendDataStatus: " << err.what() << '\n';
            }
        });

    impl->connection->enterEventLoopAsync();
    std::cout << "[DBusBackend] listening on system bus as " << BUS_NAME << '\n';

    // Idle until the daemon is told to stop. The bus does the work —
    // command requests come in via D-Bus method handlers (publishing
    // commands onto MessageBus), and outgoing signals are emitted from
    // MessageBus subscribers above. This loop just keeps the thread
    // alive and observable to running.
    while (running.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(IDLE_POLL_MS));
    }

    impl->connection->leaveEventLoop();
    impl->connected = false;
}

// NOLINTEND(readability-function-cognitive-complexity)

auto DBusBackend::stop() -> void
{
    if (!impl)
    {
        return;
    }
    if (impl->sendDataSub != 0)
    {
        MessageBus::unsubscribe(impl->sendDataSub);
        impl->sendDataSub = 0;
    }
    if (impl->exclusionSub != 0)
    {
        MessageBus::unsubscribe(impl->exclusionSub);
        impl->exclusionSub = 0;
    }
    if (impl->inclusionSub != 0)
    {
        MessageBus::unsubscribe(impl->inclusionSub);
        impl->inclusionSub = 0;
    }
    if (impl->applicationCommandSub != 0)
    {
        MessageBus::unsubscribe(impl->applicationCommandSub);
        impl->applicationCommandSub = 0;
    }
    if (impl->nodeListSub != 0)
    {
        MessageBus::unsubscribe(impl->nodeListSub);
        impl->nodeListSub = 0;
    }
    if (impl->initDataSub != 0)
    {
        MessageBus::unsubscribe(impl->initDataSub);
        impl->initDataSub = 0;
    }
    if (impl->dongleInfoSub != 0)
    {
        MessageBus::unsubscribe(impl->dongleInfoSub);
        impl->dongleInfoSub = 0;
    }
    if (impl->dongleStatusSub != 0)
    {
        MessageBus::unsubscribe(impl->dongleStatusSub);
        impl->dongleStatusSub = 0;
    }
    if (impl->connected.load() && impl->connection)
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
}
}  // namespace ExternalApi
