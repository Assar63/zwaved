#ifndef ZWAVE_TERMINAL_HANDLERS_HPP
#define ZWAVE_TERMINAL_HANDLERS_HPP

#include <cstdint>
#include <optional>
#include <vector>

// sdbus-c++ Message.h uses std::copy_n without including <algorithm>; pull it
// in first so any translation unit that includes this header compiles.
#include <algorithm>  // IWYU pragma: keep

#include <sdbus-c++/IProxy.h>

// Interactive action handlers for the zwave-terminal client: each drives one
// daemon D-Bus method (Set/Get a CC, manage associations, policy CRUD, node
// lifecycle) and logs the outcome to the activity pane. See #111.
namespace zwt
{
auto handleSwitchBinary(sdbus::IProxy& proxy, std::uint8_t& sessionCounter, bool turnOn) -> void;
auto handleSetMultilevelSwitch(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleGetMultilevelSwitch(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): proxy and counter are distinct types; method is a label
auto handleSimpleGet(sdbus::IProxy& proxy, std::uint8_t& sessionCounter, const char* method) -> void;
auto handleGetConfiguration(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleGetNotification(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleGetMeter(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleGetColorSwitch(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleSetColorSwitch(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleSetDoorLock(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleGetUserCode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleSetUserCode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleSetBasic(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleSetThermostatMode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleGetThermostatSetpoint(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleSetThermostatSetpoint(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleSetThermostatFanMode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleSetConfiguration(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleSetWakeUpInterval(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): proxy and counter are distinct types; method is a label
auto handleAssociationEdit(sdbus::IProxy& proxy, std::uint8_t& sessionCounter, const char* method) -> void;
auto handleNetworkStatus(sdbus::IProxy& proxy) -> void;
auto handleDongleInfo(sdbus::IProxy& proxy) -> void;
[[nodiscard]] auto nodeSupportsCc(const std::vector<std::uint8_t>& ccs, std::uint8_t targetCc) -> bool;
[[nodiscard]] auto fetchControllerNodeId(sdbus::IProxy& proxy) -> std::optional<std::uint8_t>;
auto handleGetAssociationGroupings(sdbus::IProxy& proxy, std::uint8_t& callbackCounter) -> void;
auto handleGetAssociation(sdbus::IProxy& proxy, std::uint8_t& callbackCounter) -> void;
auto handleSetLifeline(sdbus::IProxy& proxy, std::uint8_t& callbackCounter) -> void;
auto handleRemoveFailedNode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void;
auto handleListNodes(sdbus::IProxy& proxy) -> void;
auto handleViewEffectivePolicy(sdbus::IProxy& proxy) -> void;
auto handleViewNodeOverride(sdbus::IProxy& proxy) -> void;
auto handleDeleteNodeOverride(sdbus::IProxy& proxy) -> void;
auto handleSetNodeOverrideEntry(sdbus::IProxy& proxy) -> void;
auto handleDevicePolicyEdit(sdbus::IProxy& proxy) -> void;
auto handleListDevicePolicies(sdbus::IProxy& proxy) -> void;
auto handleListScenes(sdbus::IProxy& proxy) -> void;
auto handleListSceneTriggers(sdbus::IProxy& proxy) -> void;
auto handleSetScene(sdbus::IProxy& proxy) -> void;
auto handleDeleteScene(sdbus::IProxy& proxy) -> void;
auto handleBindSceneTrigger(sdbus::IProxy& proxy) -> void;
auto handleUnbindSceneTrigger(sdbus::IProxy& proxy) -> void;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_HANDLERS_HPP
