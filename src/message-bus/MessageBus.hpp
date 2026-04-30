#ifndef ZWAVED_MESSAGE_BUS_HPP
#define ZWAVED_MESSAGE_BUS_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/**
 * In-process publish/subscribe bus for status broadcasts. Publishers
 * emit events that any number of subscribers can consume.
 *
 * Dispatch is synchronous on the publisher's thread; handlers must
 * return promptly and not block. State events (currently just
 * DongleStatus) are retained: a new subscriber receives the most
 * recent published value once, immediately, on subscribe — so late
 * starters don't miss the current state. Handlers must not call
 * publish() or subscribe() reentrantly.
 *
 * The implementation is a thin wrapper around eventpp; this header
 * is intentionally minimal so the underlying library can be replaced
 * without touching call sites.
 */
namespace MessageBus
{
using SubscriptionId = std::uint64_t;

/// Lifecycle state of the Z-Wave dongle's serial attachment.
struct DongleStatus
{
    bool connected;
    std::string ttyPath;  // Empty unless connected.
};

/// Static introspection captured once when the daemon opens the
/// dongle's serial port: library version string + type, network home
/// ID, and this controller's own node ID. Retained on the bus so late
/// subscribers see the latest snapshot.
struct DongleInfo
{
    std::string libraryVersion;
    std::uint8_t libraryType = 0;
    std::vector<std::uint8_t> homeId;  // 4 bytes
    std::uint8_t controllerNodeId = 0;
};

/// FUNC_ID_SERIAL_API_GET_INIT_DATA (0x02) payload, captured once
/// during startup introspection. nodeIds is the expanded node bitmap
/// (every node ID currently included in the network, regardless of
/// whether the daemon has met it during this run). Retained on the
/// bus so late subscribers see the latest snapshot.
struct InitData
{
    std::uint8_t serialApiVersion = 0;
    std::uint8_t capabilities     = 0;
    std::uint8_t chipType         = 0;
    std::uint8_t chipVersion      = 0;
    std::vector<std::uint8_t> nodeIds;
};

/// Unsolicited command-class frame received from a node, carried inside
/// FUNC_ID_APPLICATION_COMMAND_HANDLER (0x04). Transient: not retained
/// across subscribes.
struct ApplicationCommand
{
    uint8_t rxStatus     = 0;
    uint8_t sourceNodeId = 0;
    std::vector<uint8_t> ccData;
};

[[nodiscard]] auto subscribe(const std::function<void(const DongleStatus&)>& handler) -> SubscriptionId;
[[nodiscard]] auto subscribe(const std::function<void(const DongleInfo&)>& handler) -> SubscriptionId;
[[nodiscard]] auto subscribe(const std::function<void(const InitData&)>& handler) -> SubscriptionId;
[[nodiscard]] auto subscribe(const std::function<void(const ApplicationCommand&)>& handler) -> SubscriptionId;
auto unsubscribe(SubscriptionId subscriptionId) -> void;
auto publish(const DongleStatus& status) -> void;
auto publish(const DongleInfo& info) -> void;
auto publish(const InitData& info) -> void;
auto publish(const ApplicationCommand& event) -> void;
}  // namespace MessageBus

#endif  // ZWAVED_MESSAGE_BUS_HPP
