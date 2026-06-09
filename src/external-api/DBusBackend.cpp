#include "DBusBackend.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../node-metadata/NodeMetadata.hpp"
#include "../policy-register/PolicyRegister.hpp"
#include "../scene-store/SceneStore.hpp"
#include "DBusBackendInternal.hpp"
#include "Version.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <memory>
#include <mutex>
#include <sstream>
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
constexpr int IDLE_POLL_MS = 200;

// Flag-byte bits for the AddNode / RemoveNode method handlers.
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
DBusBackend::DBusBackend()
    : impl(std::make_unique<Impl>())
{
}

DBusBackend::~DBusBackend()
{
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall): static dispatch of own stop() is intended for cleanup
    DBusBackend::stop();
}

// NOLINTBEGIN(readability-function-cognitive-complexity): flat signal/subscriber registration list
auto DBusBackend::run(const std::atomic<bool>& running) -> void
{
    impl->startTime = std::chrono::steady_clock::now();
    try
    {
        impl->connection = sdbus::createSystemBusConnection(BUS_NAME);
    }
    catch (const sdbus::Error& err)
    {
        Logger::error(std::string("[DBusBackend] failed to acquire system bus name ") + BUS_NAME + ": " + err.what());
        MessageBus::publish(MessageBus::DaemonError{
            .severity = MessageBus::DaemonError::SEVERITY_CRITICAL,
            .source   = "external-api",
            .code     = MessageBus::DaemonError::CODE_DBUS_BIND_FAILED,
            .message  = std::string("failed to acquire system bus name ") + BUS_NAME + ": " + err.what(),
        });
        return;
    }

    impl->object    = sdbus::createObject(*impl->connection, OBJECT_PATH);
    impl->connected = true;

    auto& obj = *impl->object;

    // Methods + signals are emitted from InterfaceManifest.yml by
    // scripts/codegen/. The five hand-written cache-update
    // subscribers below feed impl->last* state for the
    // `custom: emitGet*` handlers; subscribeGeneratedSignals()
    // appends the signal-emission subscribers, which run after the
    // cache-update subscribers so any field a D-Bus signal carries
    // is already cached by the time the signal goes out.
    registerGeneratedMethods(obj, *impl);
    registerGeneratedSignals(obj);
    obj.finishRegistration();

    auto& subs = impl->cacheSubs;
    subs.emplace_back(MessageBus::subscribe<MessageBus::DongleStatus>(
        [this](const MessageBus::DongleStatus& status) -> void
        {
            std::scoped_lock const lock(impl->stateMutex);
            impl->lastDongleStatus = status;
        }));
    subs.emplace_back(MessageBus::subscribe<MessageBus::DongleInfo>(
        [this](const MessageBus::DongleInfo& info) -> void
        {
            std::scoped_lock const lock(impl->stateMutex);
            impl->lastDongleInfo = info;
        }));
    subs.emplace_back(MessageBus::subscribe<MessageBus::InitData>(
        [this](const MessageBus::InitData& info) -> void
        {
            std::scoped_lock const lock(impl->stateMutex);
            impl->lastInitData = info;
        }));
    subs.emplace_back(MessageBus::subscribe<MessageBus::NodeListChanged>(
        [this](const MessageBus::NodeListChanged& event) -> void
        {
            std::scoped_lock const lock(impl->stateMutex);
            impl->lastNodes = event.nodes;
        }));
    subs.emplace_back(MessageBus::subscribe<MessageBus::SessionStatus>(
        [this](const MessageBus::SessionStatus& status) -> void
        {
            std::scoped_lock const lock(impl->stateMutex);
            impl->lastSessionStatus = status;
        }));
    subs.emplace_back(MessageBus::subscribe<MessageBus::DaemonError>(
        [this](const MessageBus::DaemonError& error) -> void
        {
            std::scoped_lock const lock(impl->stateMutex);
            impl->lastDaemonError = error;
        }));

    subscribeGeneratedSignals(*impl);

    impl->connection->enterEventLoopAsync();
    Logger::info(std::string("[DBusBackend] listening on system bus as ") + BUS_NAME);

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
    // Generated signal subscribers first — they read impl.connected
    // and impl.object inside their lambdas, so unhooking them before
    // the hand-written cache subs avoids the brief window where a
    // signal-emit fires after `connected = false` has been observed.
    unsubscribeGeneratedSignals(*impl);

    // Cache-update subscribers — guards' destructors call
    // MessageBus::unsubscribe; Impl's destructor would do the same if
    // stop() were skipped.
    impl->cacheSubs.clear();
    if (impl->connected.load() && impl->connection)
    {
        try
        {
            impl->connection->leaveEventLoop();
        }
        catch (const sdbus::Error& err)
        {
            Logger::error(std::string("[DBusBackend] leaveEventLoop: ") + err.what());
        }
        impl->connected = false;
    }
}

// =====================================================================
// Hand-written `custom:` method handlers — one per method whose action
// shape isn't yet expressible in the manifest. Forward-declared by the
// generated DBusMethods.gen.hpp; called from
// registerGeneratedMethods() via thin per-method lambdas.
// =====================================================================

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitAddNode(DBusBackend::Impl& /*impl*/,
                 std::uint8_t mode,
                 std::uint8_t flags,
                 std::uint8_t sessionId,
                 const std::vector<std::uint8_t>& nwiHomeId,
                 const std::vector<std::uint8_t>& authHomeId) -> void
{
    MessageBus::publish(MessageBus::AddNodeCommand{.mode              = mode,
                                                   .power             = bitSet(flags, FLAG_POWER_BIT),
                                                   .nwi               = bitSet(flags, FLAG_NWI_BIT),
                                                   .protocolLongRange = bitSet(flags, FLAG_PROTOCOL_BIT),
                                                   .skipFlNeighbors   = bitSet(flags, FLAG_SFLND_BIT),
                                                   .sessionId         = sessionId,
                                                   .includeHomeIds    = !nwiHomeId.empty() || !authHomeId.empty(),
                                                   .nwiHomeId         = homeIdFromVector(nwiHomeId),
                                                   .authHomeId        = homeIdFromVector(authHomeId)});
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitRemoveNode(DBusBackend::Impl& /*impl*/, std::uint8_t mode, std::uint8_t flags, std::uint8_t sessionId) -> void
{
    MessageBus::publish(MessageBus::RemoveNodeCommand{.mode      = mode,
                                                      .power     = bitSet(flags, FLAG_POWER_BIT),
                                                      .nwe       = bitSet(flags, FLAG_NWE_BIT),
                                                      .sessionId = sessionId});
}

namespace
{
auto convertEndpointPairs(const std::vector<EndpointPair>& pairs) -> std::vector<MessageBus::EndpointMember>
{
    std::vector<MessageBus::EndpointMember> result;
    result.reserve(pairs.size());
    for (const auto& pair : pairs)
    {
        result.push_back(MessageBus::EndpointMember{.nodeId = pair.get<0>(), .endpoint = pair.get<1>()});
    }
    return result;
}
}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitSetMultichannelAssociation(DBusBackend::Impl& /*impl*/,
                                    std::uint8_t nodeId,
                                    std::uint8_t groupId,
                                    const std::vector<std::uint8_t>& nodeMembers,
                                    const std::vector<EndpointPair>& endpointMembers,
                                    std::uint8_t callbackId) -> void
{
    MessageBus::publish(
        MessageBus::SetMultichannelAssociationCommand{.nodeId          = nodeId,
                                                      .groupId         = groupId,
                                                      .nodeMembers     = nodeMembers,
                                                      .endpointMembers = convertEndpointPairs(endpointMembers),
                                                      .callbackId      = callbackId});
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitRemoveMultichannelAssociation(DBusBackend::Impl& /*impl*/,
                                       std::uint8_t nodeId,
                                       std::uint8_t groupId,
                                       const std::vector<std::uint8_t>& nodeMembers,
                                       const std::vector<EndpointPair>& endpointMembers,
                                       std::uint8_t callbackId) -> void
{
    MessageBus::publish(
        MessageBus::RemoveMultichannelAssociationCommand{.nodeId          = nodeId,
                                                         .groupId         = groupId,
                                                         .nodeMembers     = nodeMembers,
                                                         .endpointMembers = convertEndpointPairs(endpointMembers),
                                                         .callbackId      = callbackId});
}

auto emitGetNodes(DBusBackend::Impl& impl) -> std::vector<NodeTuple>
{
    std::vector<NodeTuple> result;
    std::scoped_lock const lock(impl.stateMutex);
    result.reserve(impl.lastNodes.size());
    for (const auto& info : impl.lastNodes)
    {
        result.emplace_back(info.nodeId, info.basicType, info.genericType, info.specificType, info.commandClasses);
    }
    return result;
}

auto emitGetDongleInfo(DBusBackend::Impl& impl) -> DongleInfoTuple
{
    std::scoped_lock const lock(impl.stateMutex);
    return DongleInfoTuple{impl.lastDongleInfo.libraryVersion,
                           impl.lastDongleInfo.libraryType,
                           impl.lastDongleInfo.homeId,
                           impl.lastDongleInfo.controllerNodeId};
}

auto emitGetInitData(DBusBackend::Impl& impl) -> InitDataTuple
{
    std::scoped_lock const lock(impl.stateMutex);
    return InitDataTuple{impl.lastInitData.serialApiVersion,
                         impl.lastInitData.capabilities,
                         impl.lastInitData.nodeIds,
                         impl.lastInitData.chipType,
                         impl.lastInitData.chipVersion};
}

auto emitGetVersion(DBusBackend::Impl& /*impl*/) -> DaemonVersionTuple
{
    return DaemonVersionTuple{Version::SEMVER, Version::GIT_DESCRIBE};
}

auto emitGetNetworkStatus(DBusBackend::Impl& impl) -> NetworkStatusTuple
{
    std::scoped_lock const lock(impl.stateMutex);

    // Hex-format the home ID (4 bytes from MEMORY_GET_ID) for human
    // readability — matches what NodeRegistry logs and what's keyed
    // into the SQLite db.
    std::ostringstream homeIdStream;
    homeIdStream << std::hex << std::uppercase << std::setfill('0');
    for (const auto byte : impl.lastDongleInfo.homeId)
    {
        homeIdStream << std::setw(2) << static_cast<unsigned>(byte);
    }

    const auto uptime =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - impl.startTime).count();

    return NetworkStatusTuple{impl.lastDongleStatus.connected,
                              impl.lastDongleStatus.ttyPath,
                              homeIdStream.str(),
                              impl.lastDongleInfo.controllerNodeId,
                              static_cast<std::uint32_t>(impl.lastNodes.size()),
                              impl.lastSessionStatus.active,
                              impl.lastSessionStatus.commandId,
                              impl.lastSessionStatus.sessionId,
                              static_cast<std::uint64_t>(uptime)};
}

auto emitGetDaemonError(DBusBackend::Impl& impl) -> DaemonErrorTuple
{
    std::scoped_lock const lock(impl.stateMutex);
    return DaemonErrorTuple{impl.lastDaemonError.severity,
                            impl.lastDaemonError.source,
                            impl.lastDaemonError.code,
                            impl.lastDaemonError.message};
}

// ---- Policy CRUD (#69) ----------------------------------------------
// Thin adapters over PolicyRegister::instance(). `policy` crosses the
// wire as the register's serialized BLOB, so Set deserializes inbound
// bytes and Get/List serialize outbound. PolicyRegister publishes
// PolicyChanged from inside its mutators, so the orchestrators
// re-evaluate without extra plumbing here.

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitSetDevicePolicy(DBusBackend::Impl& /*impl*/,
                         std::uint16_t manufacturerId,
                         std::uint16_t productTypeId,
                         std::uint16_t productId,
                         const std::vector<std::uint8_t>& policy) -> void
{
    const auto decoded = PolicyRegister::deserialize(policy);
    if (!decoded.has_value())
    {
        Logger::warn("[DBusBackend] SetDevicePolicy: malformed policy blob, ignored");
        return;
    }
    PolicyRegister::instance().setDevicePolicy(
        PolicyRegister::DeviceId{
            .manufacturerId = manufacturerId, .productTypeId = productTypeId, .productId = productId},
        *decoded);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitGetDevicePolicy(DBusBackend::Impl& /*impl*/,
                         std::uint16_t manufacturerId,
                         std::uint16_t productTypeId,
                         std::uint16_t productId) -> std::vector<std::uint8_t>
{
    const auto policy = PolicyRegister::instance().devicePolicy(PolicyRegister::DeviceId{
        .manufacturerId = manufacturerId, .productTypeId = productTypeId, .productId = productId});
    return policy.has_value() ? PolicyRegister::serialize(*policy) : std::vector<std::uint8_t>{};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitDeleteDevicePolicy(DBusBackend::Impl& /*impl*/,
                            std::uint16_t manufacturerId,
                            std::uint16_t productTypeId,
                            std::uint16_t productId) -> void
{
    PolicyRegister::instance().deleteDevicePolicy(PolicyRegister::DeviceId{
        .manufacturerId = manufacturerId, .productTypeId = productTypeId, .productId = productId});
}

auto emitListDevicePolicies(DBusBackend::Impl& /*impl*/) -> std::vector<DevicePolicyTuple>
{
    std::vector<DevicePolicyTuple> out;
    for (const auto& row : PolicyRegister::instance().listDevicePolicies())
    {
        out.emplace_back(row.device.manufacturerId,
                         row.device.productTypeId,
                         row.device.productId,
                         PolicyRegister::serialize(row.policy));
    }
    return out;
}

auto emitSetNodeOverride(DBusBackend::Impl& /*impl*/,
                         std::uint8_t nodeId,
                         const std::vector<std::uint8_t>& policy) -> void
{
    const auto decoded = PolicyRegister::deserialize(policy);
    if (!decoded.has_value())
    {
        Logger::warn("[DBusBackend] SetNodeOverride: malformed policy blob, ignored");
        return;
    }
    PolicyRegister::instance().setNodeOverride(nodeId, *decoded);
}

auto emitGetNodeOverride(DBusBackend::Impl& /*impl*/, std::uint8_t nodeId) -> std::vector<std::uint8_t>
{
    const auto policy = PolicyRegister::instance().nodeOverride(nodeId);
    return policy.has_value() ? PolicyRegister::serialize(*policy) : std::vector<std::uint8_t>{};
}

auto emitDeleteNodeOverride(DBusBackend::Impl& /*impl*/, std::uint8_t nodeId) -> void
{
    PolicyRegister::instance().deleteNodeOverride(nodeId);
}

auto emitGetEffectivePolicy(DBusBackend::Impl& /*impl*/, std::uint8_t nodeId) -> std::vector<std::uint8_t>
{
    return PolicyRegister::serialize(PolicyRegister::instance().effectivePolicy(nodeId));
}

// ---- Node metadata CRUD (#83) ---------------------------------------
// Thin adapters over NodeMetadata::instance(). Set with an empty value
// clears the key (the store handles that idiom); the store publishes
// NodeMetadataChanged from inside its mutators.

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitSetNodeMetadata(DBusBackend::Impl& /*impl*/,
                         std::uint8_t nodeId,
                         const std::string& key,
                         const std::string& value) -> void
{
    NodeMetadata::instance().set(nodeId, key, value);
}

auto emitGetNodeMetadata(DBusBackend::Impl& /*impl*/, std::uint8_t nodeId) -> std::vector<NodeMetadataTuple>
{
    std::vector<NodeMetadataTuple> out;
    for (const auto& entry : NodeMetadata::instance().getAll(nodeId))
    {
        out.emplace_back(entry.key, entry.value);
    }
    return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitDeleteNodeMetadata(DBusBackend::Impl& /*impl*/, std::uint8_t nodeId, const std::string& key) -> void
{
    NodeMetadata::instance().remove(nodeId, key);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitGetNodesByMetadata(DBusBackend::Impl& /*impl*/,
                            const std::string& key,
                            const std::string& value) -> std::vector<std::uint8_t>
{
    return NodeMetadata::instance().nodesWith(key, value);
}

// ---- Scene + trigger CRUD (#122) -------------------------------------
// Thin adapters over SceneStore::instance(). A scene crosses the wire as
// a(yay) — a list of (targetNodeId, ccPayload) actions; a trigger as
// a(yyys). The store owns persistence only; the SceneOrchestrator (#121)
// drives the runtime side off CentralSceneNotification, so nothing here
// touches the bus.

namespace
{
auto convertSceneActions(const std::vector<SceneActionEntry>& wire) -> std::vector<SceneStore::Action>
{
    std::vector<SceneStore::Action> out;
    out.reserve(wire.size());
    for (const auto& entry : wire)
    {
        out.push_back(SceneStore::Action{.targetNodeId = entry.get<0>(), .ccPayload = entry.get<1>()});
    }
    return out;
}
}  // namespace

auto emitSetScene(DBusBackend::Impl& /*impl*/,
                  const std::string& sceneId,
                  const std::vector<SceneActionEntry>& actions) -> void
{
    SceneStore::instance().setScene(sceneId, convertSceneActions(actions));
}

auto emitGetScene(DBusBackend::Impl& /*impl*/, const std::string& sceneId) -> std::vector<SceneActionEntry>
{
    std::vector<SceneActionEntry> out;
    const auto actions = SceneStore::instance().getScene(sceneId);
    if (actions.has_value())
    {
        for (const auto& action : *actions)
        {
            out.emplace_back(action.targetNodeId, action.ccPayload);
        }
    }
    return out;
}

auto emitDeleteScene(DBusBackend::Impl& /*impl*/, const std::string& sceneId) -> void
{
    SceneStore::instance().deleteScene(sceneId);
}

auto emitListScenes(DBusBackend::Impl& /*impl*/) -> std::vector<std::string>
{
    return SceneStore::instance().listScenes();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitBindSceneTrigger(DBusBackend::Impl& /*impl*/,
                          std::uint8_t source,
                          std::uint8_t sourceNodeId,
                          std::uint8_t sceneNumber,
                          std::uint8_t keyAttribute,
                          const std::string& sceneId) -> void
{
    SceneStore::instance().bindTrigger(source, sourceNodeId, sceneNumber, keyAttribute, sceneId);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus method
auto emitUnbindSceneTrigger(DBusBackend::Impl& /*impl*/,
                            std::uint8_t source,
                            std::uint8_t sourceNodeId,
                            std::uint8_t sceneNumber,
                            std::uint8_t keyAttribute) -> void
{
    SceneStore::instance().unbindTrigger(source, sourceNodeId, sceneNumber, keyAttribute);
}

auto emitListSceneTriggers(DBusBackend::Impl& /*impl*/) -> std::vector<SceneTriggerEntry>
{
    std::vector<SceneTriggerEntry> out;
    for (const auto& trigger : SceneStore::instance().listTriggers())
    {
        out.emplace_back(
            trigger.source, trigger.sourceNodeId, trigger.sceneNumber, trigger.keyAttribute, trigger.sceneId);
    }
    return out;
}
}  // namespace ExternalApi
