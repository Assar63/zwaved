#ifndef ZWAVED_DBUS_BACKEND_INTERNAL_HPP
#define ZWAVED_DBUS_BACKEND_INTERNAL_HPP

// IWYU pragma: begin_exports
#include "../message-bus/MessageBus.hpp"
#include "DBusBackend.hpp"
// IWYU pragma: end_exports

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
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

using DongleInfoTuple = sdbus::Struct<std::string, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t>;

using InitDataTuple = sdbus::Struct<std::uint8_t, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t, std::uint8_t>;

using DaemonVersionTuple = sdbus::Struct<std::string, std::string>;

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

    MessageBus::SubscriptionId dongleStatusSub{0};
    MessageBus::SubscriptionId dongleInfoSub{0};
    MessageBus::SubscriptionId initDataSub{0};
    MessageBus::SubscriptionId applicationCommandSub{0};
    MessageBus::SubscriptionId nodeListSub{0};
    MessageBus::SubscriptionId inclusionSub{0};
    MessageBus::SubscriptionId exclusionSub{0};
    MessageBus::SubscriptionId sendDataSub{0};
    MessageBus::SubscriptionId removeFailedNodeSub{0};
    MessageBus::SubscriptionId sessionStatusSub{0};

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

    // Captured the first time `run()` is called; powers the uptime
    // field of GetNetworkStatus. steady_clock so it doesn't jump
    // around if the wall clock is stepped.
    std::chrono::steady_clock::time_point startTime;
};
}  // namespace ExternalApi

// IWYU pragma: begin_exports
#include "DBusMethods.gen.hpp"
// IWYU pragma: end_exports

#endif  // ZWAVED_DBUS_BACKEND_INTERNAL_HPP
