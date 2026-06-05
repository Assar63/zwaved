#include "handlers.hpp"

#include "activity.hpp"
#include "constants.hpp"
#include "format.hpp"
#include "policy_blob.hpp"
#include "prompts.hpp"

#include <cstdint>
#include <iomanip>
#include <ios>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>

namespace zwt
{
auto handleSwitchBinary(sdbus::IProxy& proxy, std::uint8_t& sessionCounter, bool turnOn) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("SetSwitchBinary: cancelled or invalid node id");
        return;
    }
    ++sessionCounter;
    proxy.callMethod("SetSwitchBinary").onInterface(IFACE_NAME).withArguments(*nodeId, turnOn, sessionCounter);
    std::ostringstream stream;
    stream << "SetSwitchBinary node=" << static_cast<unsigned>(*nodeId) << " " << (turnOn ? "ON" : "OFF")
           << " callback=" << static_cast<unsigned>(sessionCounter);
    logLine(stream.str());
}

auto handleSetMultilevelSwitch(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("SetMultilevelSwitch: cancelled or invalid node id");
        return;
    }
    // Level range covers 0=off, 1..99=dimmer level, plus 0xFF=restore-last
    // and 0xFE=unknown sentinel. promptByte's [0,255] is the simplest
    // bound; we let the user pick any byte and the spec semantics do
    // the rest.
    auto level = promptByte("Level (0=off, 1-99=dim, 255=restore):", BYTE_MIN, BYTE_MAX);
    if (!level.has_value())
    {
        logLine("SetMultilevelSwitch: cancelled or invalid level");
        return;
    }
    auto duration = promptByte("Duration (0=instant, 1-127=sec, 128-254=min, 255=default):", BYTE_MIN, BYTE_MAX);
    if (!duration.has_value())
    {
        logLine("SetMultilevelSwitch: cancelled or invalid duration");
        return;
    }
    ++sessionCounter;
    proxy.callMethod("SetMultilevelSwitch")
        .onInterface(IFACE_NAME)
        .withArguments(*nodeId, *level, *duration, sessionCounter);
    std::ostringstream stream;
    stream << "SetMultilevelSwitch node=" << static_cast<unsigned>(*nodeId)
           << " level=" << static_cast<unsigned>(*level) << " duration=0x" << std::hex << std::setw(2)
           << std::setfill('0') << static_cast<unsigned>(*duration) << std::dec
           << " callback=" << static_cast<unsigned>(sessionCounter);
    logLine(stream.str());
}

auto handleGetMultilevelSwitch(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("GetMultilevelSwitch: cancelled or invalid node id");
        return;
    }
    ++sessionCounter;
    proxy.callMethod("GetMultilevelSwitch").onInterface(IFACE_NAME).withArguments(*nodeId, sessionCounter);
    std::ostringstream stream;
    stream << "GetMultilevelSwitch node=" << static_cast<unsigned>(*nodeId)
           << " callback=" << static_cast<unsigned>(sessionCounter);
    logLine(stream.str());
}

/// Drive a simple `(nodeId, callbackId)` GET method (Battery, Version,
/// Manufacturer Specific, Z-Wave Plus Info). The decoded answer arrives
/// asynchronously as the matching typed report signal.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): proxy and counter are distinct types; method is a label
auto handleSimpleGet(sdbus::IProxy& proxy, std::uint8_t& sessionCounter, const char* method) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine(std::string{method} + ": cancelled or invalid node id");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod(method).onInterface(IFACE_NAME).withArguments(*nodeId, sessionCounter);
        logLine(std::string{method} + " node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{method} + " failed: " + err.what());
    }
}

auto handleGetConfiguration(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("GetConfiguration: cancelled or invalid node id");
        return;
    }
    auto parameter = promptByte("Config parameter (0-255):", BYTE_MIN, BYTE_MAX);
    if (!parameter.has_value())
    {
        logLine("GetConfiguration: cancelled or invalid parameter");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("GetConfiguration").onInterface(IFACE_NAME).withArguments(*nodeId, *parameter, sessionCounter);
        std::ostringstream stream;
        stream << "GetConfiguration node=" << static_cast<unsigned>(*nodeId)
               << " param=" << static_cast<unsigned>(*parameter)
               << " callback=" << static_cast<unsigned>(sessionCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetConfiguration failed: "} + err.what());
    }
}

auto handleGetNotification(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("GetNotification: cancelled or invalid node id");
        return;
    }
    auto notificationType = promptByte("Notification type (0-255):", BYTE_MIN, BYTE_MAX);
    if (!notificationType.has_value())
    {
        logLine("GetNotification: cancelled or invalid notification type");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("GetNotification")
            .onInterface(IFACE_NAME)
            .withArguments(*nodeId, *notificationType, sessionCounter);
        std::ostringstream stream;
        stream << "GetNotification node=" << static_cast<unsigned>(*nodeId) << " type=" << std::hex << "0x"
               << static_cast<unsigned>(*notificationType) << std::dec
               << " callback=" << static_cast<unsigned>(sessionCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetNotification failed: "} + err.what());
    }
}

auto handleGetMeter(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("GetMeter: cancelled or invalid node id");
        return;
    }
    auto scale = promptByte("Meter scale (0=kWh, 2=W, …):", BYTE_MIN, BYTE_MAX);
    if (!scale.has_value())
    {
        logLine("GetMeter: cancelled or invalid scale");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("GetMeter").onInterface(IFACE_NAME).withArguments(*nodeId, *scale, sessionCounter);
        std::ostringstream stream;
        stream << "GetMeter node=" << static_cast<unsigned>(*nodeId) << " scale=" << static_cast<unsigned>(*scale)
               << " callback=" << static_cast<unsigned>(sessionCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetMeter failed: "} + err.what());
    }
}

auto handleGetColorSwitch(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("GetColorSwitch: cancelled or invalid node id");
        return;
    }
    auto componentId = promptByte("Component (2=red, 3=green, 4=blue, …):", BYTE_MIN, BYTE_MAX);
    if (!componentId.has_value())
    {
        logLine("GetColorSwitch: cancelled or invalid component");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("GetColorSwitch").onInterface(IFACE_NAME).withArguments(*nodeId, *componentId, sessionCounter);
        logLine("GetColorSwitch node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " component=" + std::to_string(static_cast<unsigned>(*componentId)) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetColorSwitch failed: "} + err.what());
    }
}

auto handleSetColorSwitch(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    auto red    = promptByte("Red (0-255):", BYTE_MIN, BYTE_MAX);
    auto green  = promptByte("Green (0-255):", BYTE_MIN, BYTE_MAX);
    auto blue   = promptByte("Blue (0-255):", BYTE_MIN, BYTE_MAX);
    if (!nodeId.has_value() || !red.has_value() || !green.has_value() || !blue.has_value())
    {
        logLine("SetColorSwitch: cancelled or invalid");
        return;
    }
    // Flat (componentId, value) pairs for red/green/blue; duration = default.
    const std::vector<std::uint8_t> components{2, *red, 3, *green, 4, *blue};
    ++sessionCounter;
    try
    {
        proxy.callMethod("SetColorSwitch")
            .onInterface(IFACE_NAME)
            .withArguments(*nodeId, components, DURATION_DEFAULT, sessionCounter);
        logLine("SetColorSwitch node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " rgb=" + std::to_string(static_cast<unsigned>(*red)) + "," +
                std::to_string(static_cast<unsigned>(*green)) + "," + std::to_string(static_cast<unsigned>(*blue)) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetColorSwitch failed: "} + err.what());
    }
}

auto handleSetDoorLock(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    auto mode   = promptByte("Lock mode (0x00 unsecured, 0xFF secured):", BYTE_MIN, BYTE_MAX);
    if (!nodeId.has_value() || !mode.has_value())
    {
        logLine("SetDoorLock: cancelled or invalid");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("SetDoorLock").onInterface(IFACE_NAME).withArguments(*nodeId, *mode, sessionCounter);
        logLine("SetDoorLock node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " mode=" + std::to_string(static_cast<unsigned>(*mode)) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetDoorLock failed: "} + err.what());
    }
}

auto handleGetUserCode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("GetUserCode: cancelled or invalid node id");
        return;
    }
    auto slot = promptByte("User code slot (1-255):", BYTE_MIN, BYTE_MAX);
    if (!slot.has_value())
    {
        logLine("GetUserCode: cancelled or invalid slot");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("GetUserCode").onInterface(IFACE_NAME).withArguments(*nodeId, *slot, sessionCounter);
        logLine("GetUserCode node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " slot=" + std::to_string(static_cast<unsigned>(*slot)) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetUserCode failed: "} + err.what());
    }
}

auto handleSetUserCode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    auto slot   = promptByte("User code slot (1-255):", BYTE_MIN, BYTE_MAX);
    auto status = promptByte("Status (0x00 available/clear, 0x01 enabled):", BYTE_MIN, BYTE_MAX);
    auto code   = promptLine("Code (4-10 digits; blank to clear):");
    if (!nodeId.has_value() || !slot.has_value() || !status.has_value() || !code.has_value())
    {
        logLine("SetUserCode: cancelled or invalid");
        return;
    }
    const std::vector<std::uint8_t> codeBytes(code->begin(), code->end());
    ++sessionCounter;
    try
    {
        proxy.callMethod("SetUserCode")
            .onInterface(IFACE_NAME)
            .withArguments(*nodeId, *slot, *status, codeBytes, sessionCounter);
        logLine("SetUserCode node=" + std::to_string(static_cast<unsigned>(*nodeId)) + " slot=" +
                std::to_string(static_cast<unsigned>(*slot)) + " codeLen=" + std::to_string(codeBytes.size()) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetUserCode failed: "} + err.what());
    }
}

// ---- Node control for non-binary CCs (#47) --------------------------
// Drive the daemon's Set methods for CCs beyond binary switch. Each is a
// fire-and-forget SendData; completion arrives as SendDataStatus and any
// reply as the matching typed report signal.

auto handleSetBasic(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    auto value  = promptByte("Basic value (0=off, 0xFF=on, 1-99=level):", BYTE_MIN, BYTE_MAX);
    if (!nodeId.has_value() || !value.has_value())
    {
        logLine("SetBasic: cancelled or invalid");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("SetBasic").onInterface(IFACE_NAME).withArguments(*nodeId, *value, sessionCounter);
        logLine("SetBasic node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " value=" + std::to_string(static_cast<unsigned>(*value)) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetBasic failed: "} + err.what());
    }
}

auto handleSetThermostatMode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    auto mode   = promptByte("Thermostat mode (0=off, 1=heat, 2=cool, 3=auto):", BYTE_MIN, BYTE_MAX);
    if (!nodeId.has_value() || !mode.has_value())
    {
        logLine("SetThermostatMode: cancelled or invalid");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("SetThermostatMode").onInterface(IFACE_NAME).withArguments(*nodeId, *mode, sessionCounter);
        logLine("SetThermostatMode node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " mode=" + std::to_string(static_cast<unsigned>(*mode)) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetThermostatMode failed: "} + err.what());
    }
}

auto handleGetThermostatSetpoint(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("GetThermostatSetpoint: cancelled or invalid node id");
        return;
    }
    auto setpointType = promptByte("Setpoint type (1=heating, 2=cooling):", BYTE_MIN, BYTE_MAX);
    if (!setpointType.has_value())
    {
        logLine("GetThermostatSetpoint: cancelled or invalid setpoint type");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("GetThermostatSetpoint")
            .onInterface(IFACE_NAME)
            .withArguments(*nodeId, *setpointType, sessionCounter);
        logLine("GetThermostatSetpoint node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " type=" + std::to_string(static_cast<unsigned>(*setpointType)) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetThermostatSetpoint failed: "} + err.what());
    }
}

auto handleSetThermostatSetpoint(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId       = promptNodeId("Node ID (1-232):");
    auto setpointType = promptByte("Setpoint type (1=heating, 2=cooling):", BYTE_MIN, BYTE_MAX);
    auto precision    = promptByte("Precision (decimals, e.g. 1):", BYTE_MIN, BYTE_MAX);
    auto scale        = promptByte("Scale (0=C, 1=F):", BYTE_MIN, BYTE_MAX);
    auto value        = promptInt32("Raw value (e.g. 215 for 21.5 at precision 1):");
    if (!nodeId.has_value() || !setpointType.has_value() || !precision.has_value() || !scale.has_value() ||
        !value.has_value())
    {
        logLine("SetThermostatSetpoint: cancelled or invalid");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("SetThermostatSetpoint")
            .onInterface(IFACE_NAME)
            .withArguments(*nodeId, *setpointType, *precision, *scale, *value, sessionCounter);
        logLine("SetThermostatSetpoint node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " type=" + std::to_string(static_cast<unsigned>(*setpointType)) + " value=" + std::to_string(*value) +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetThermostatSetpoint failed: "} + err.what());
    }
}

auto handleSetThermostatFanMode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    auto mode   = promptByte("Fan mode (0=auto low, 1=low, 2=auto high, 3=high):", BYTE_MIN, BYTE_MAX);
    auto offVal = promptByte("Fan off? (0=no, 1=yes):", BYTE_MIN, BYTE_MAX);
    if (!nodeId.has_value() || !mode.has_value() || !offVal.has_value())
    {
        logLine("SetThermostatFanMode: cancelled or invalid");
        return;
    }
    const bool off = *offVal != 0;
    ++sessionCounter;
    try
    {
        proxy.callMethod("SetThermostatFanMode")
            .onInterface(IFACE_NAME)
            .withArguments(*nodeId, *mode, off, sessionCounter);
        logLine("SetThermostatFanMode node=" + std::to_string(static_cast<unsigned>(*nodeId)) +
                " mode=" + std::to_string(static_cast<unsigned>(*mode)) + (off ? " off" : "") +
                " callback=" + std::to_string(static_cast<unsigned>(sessionCounter)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetThermostatFanMode failed: "} + err.what());
    }
}

auto handleSetConfiguration(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId    = promptNodeId("Node ID (1-232):");
    auto parameter = promptByte("Config parameter (0-255):", BYTE_MIN, BYTE_MAX);
    auto size      = promptByte("Value size bytes (1, 2, or 4):", CONFIG_SIZE_MIN, CONFIG_SIZE_MAX);
    auto value     = promptInt32("Value (signed int32):");
    if (!nodeId.has_value() || !parameter.has_value() || !value.has_value() || !size.has_value() ||
        (*size != 1 && *size != 2 && *size != 4))
    {
        logLine("SetConfiguration: cancelled or invalid (size must be 1/2/4)");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("SetConfiguration")
            .onInterface(IFACE_NAME)
            .withArguments(*nodeId, *parameter, *size, *value < 0, *value, sessionCounter);
        std::ostringstream stream;
        stream << "SetConfiguration node=" << static_cast<unsigned>(*nodeId)
               << " param=" << static_cast<unsigned>(*parameter) << " size=" << static_cast<unsigned>(*size)
               << " value=" << *value << " callback=" << static_cast<unsigned>(sessionCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetConfiguration failed: "} + err.what());
    }
}

auto handleSetWakeUpInterval(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId  = promptNodeId("Node ID (1-232):");
    auto seconds = promptU32("Interval seconds (0..16777215):");
    auto notify  = promptByte("Notify node id (0=controller):", BYTE_MIN, BYTE_MAX);
    if (!nodeId.has_value() || !seconds.has_value() || !notify.has_value())
    {
        logLine("SetWakeUpInterval: cancelled or invalid");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("SetWakeUpInterval")
            .onInterface(IFACE_NAME)
            .withArguments(*nodeId, *seconds, *notify, sessionCounter);
        std::ostringstream stream;
        stream << "SetWakeUpInterval node=" << static_cast<unsigned>(*nodeId) << " interval=" << *seconds
               << "s notify=" << static_cast<unsigned>(*notify)
               << " callback=" << static_cast<unsigned>(sessionCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetWakeUpInterval failed: "} + err.what());
    }
}

// Add or remove association members for a group. `method` is
// "SetAssociation" (add) or "RemoveAssociation".
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): proxy and counter are distinct types; method is a label
auto handleAssociationEdit(sdbus::IProxy& proxy, std::uint8_t& sessionCounter, const char* method) -> void
{
    auto nodeId  = promptNodeId("Node ID (1-232):");
    auto groupId = promptByte("Group id (1-255):", GROUP_ID_MIN, GROUP_ID_MAX);
    auto members = promptNodeList("Member node ids (space/comma separated):");
    if (!nodeId.has_value() || !groupId.has_value() || !members.has_value())
    {
        logLine(std::string{method} + ": cancelled or invalid");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod(method).onInterface(IFACE_NAME).withArguments(*nodeId, *groupId, *members, sessionCounter);
        std::ostringstream stream;
        stream << method << " node=" << static_cast<unsigned>(*nodeId) << " group=" << static_cast<unsigned>(*groupId)
               << " members=" << members->size() << " callback=" << static_cast<unsigned>(sessionCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{method} + " failed: " + err.what());
    }
}


// ---- Policy (#66/#69) ------------------------------------------------
// A decoded policy entry. Tagged by `kind`; only the matching fields are
// meaningful. Mirrors PolicyRegister::PolicyEntry without pulling in the
// daemon's variant type.

auto handleNetworkStatus(sdbus::IProxy& proxy) -> void
{
    using NetworkStatusTuple = sdbus::Struct<bool,
                                             std::string,
                                             std::string,
                                             std::uint8_t,
                                             std::uint32_t,
                                             bool,
                                             std::uint8_t,
                                             std::uint8_t,
                                             std::uint64_t>;
    NetworkStatusTuple status;
    try
    {
        proxy.callMethod("GetNetworkStatus").onInterface(IFACE_NAME).storeResultsTo(status);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetNetworkStatus failed: "} + err.what());
        return;
    }
    const auto dongleConnected  = std::get<0>(status);
    const auto& ttyPath         = std::get<1>(status);
    const auto& homeId          = std::get<2>(status);
    const auto controllerNodeId = std::get<3>(status);
    const auto nodeCount        = std::get<4>(status);
    const auto sessionActive    = std::get<5>(status);
    const auto sessionCommandId = std::get<6>(status);
    const auto sessionId        = std::get<7>(status);
    const auto uptimeSeconds    = std::get<8>(status);

    logLine("Network status:");
    logLine(std::string("  dongle: ") + (dongleConnected ? "connected " + ttyPath : "disconnected"));
    if (!homeId.empty())
    {
        std::ostringstream stream;
        stream << "  home id: " << homeId << " (controller node " << static_cast<unsigned>(controllerNodeId) << ")";
        logLine(stream.str());
    }
    else
    {
        logLine("  home id: (not yet introspected)");
    }
    logLine("  nodes: " + std::to_string(nodeCount));
    if (sessionActive)
    {
        const char* operation = "?";
        if (sessionCommandId == CMD_ADD_NODE)
        {
            operation = "inclusion";
        }
        else if (sessionCommandId == CMD_REMOVE_NODE)
        {
            operation = "exclusion";
        }
        logLine(std::string("  active session: ") + operation + " #" +
                std::to_string(static_cast<unsigned>(sessionId)));
    }
    else
    {
        logLine("  active session: none");
    }
    const auto hours   = uptimeSeconds / SECONDS_PER_HOUR;
    const auto minutes = (uptimeSeconds % SECONDS_PER_HOUR) / SECONDS_PER_MINUTE;
    const auto seconds = uptimeSeconds % SECONDS_PER_MINUTE;
    std::ostringstream upStream;
    upStream << "  uptime: " << hours << "h " << minutes << "m " << seconds << "s";
    logLine(upStream.str());
}

auto handleDongleInfo(sdbus::IProxy& proxy) -> void
{
    using DongleInfoTuple = sdbus::Struct<std::string, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t>;
    DongleInfoTuple info;
    try
    {
        proxy.callMethod("GetDongleInfo").onInterface(IFACE_NAME).storeResultsTo(info);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetDongleInfo failed: "} + err.what());
        return;
    }
    const auto& libraryVersion  = std::get<0>(info);
    const auto libraryType      = std::get<1>(info);
    const auto& homeId          = std::get<2>(info);
    const auto controllerNodeId = std::get<3>(info);
    if (libraryVersion.empty() && libraryType == 0)
    {
        logLine("DongleInfo: (not yet introspected — connect a dongle first)");
        return;
    }
    std::ostringstream stream;
    stream << "DongleInfo: \"" << libraryVersion << "\" libType=" << static_cast<unsigned>(libraryType) << " ("
           << libraryTypeName(libraryType) << ") homeId=";
    for (const auto byte : homeId)
    {
        stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
    }
    stream << std::dec << " controllerNode=" << static_cast<unsigned>(controllerNodeId);
    logLine(stream.str());
}

/// True if `targetCc` appears in `ccs` *before* COMMAND_CLASS_MARK,
/// i.e. the node will respond to it as a target. Anything after the
/// mark is the node's controlled set, which it emits to others —
/// listing those there does not mean the node responds to that CC.
auto nodeSupportsCc(const std::vector<std::uint8_t>& ccs, std::uint8_t targetCc) -> bool
{
    for (const auto byte : ccs)
    {
        if (byte == CC_MARK)
        {
            return false;
        }
        if (byte == targetCc)
        {
            return true;
        }
    }
    return false;
}

auto fetchControllerNodeId(sdbus::IProxy& proxy) -> std::optional<std::uint8_t>
{
    using DongleInfoTuple = sdbus::Struct<std::string, std::uint8_t, std::vector<std::uint8_t>, std::uint8_t>;
    DongleInfoTuple info;
    try
    {
        proxy.callMethod("GetDongleInfo").onInterface(IFACE_NAME).storeResultsTo(info);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetDongleInfo failed: "} + err.what());
        return std::nullopt;
    }
    const auto controllerNodeId = std::get<3>(info);
    if (controllerNodeId == 0)
    {
        return std::nullopt;
    }
    return controllerNodeId;
}

auto handleGetAssociationGroupings(sdbus::IProxy& proxy, std::uint8_t& callbackCounter) -> void
{
    auto nodeId = promptByte("Node ID (1-232):", NODE_ID_MIN, NODE_ID_MAX);
    if (!nodeId.has_value())
    {
        logLine("GetAssociationGroupings: cancelled or invalid node id");
        return;
    }
    ++callbackCounter;
    try
    {
        proxy.callMethod("GetAssociationGroupings").onInterface(IFACE_NAME).withArguments(*nodeId, callbackCounter);
        std::ostringstream stream;
        stream << "GetAssociationGroupings node=" << static_cast<unsigned>(*nodeId)
               << " callback=" << static_cast<unsigned>(callbackCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetAssociationGroupings failed: "} + err.what());
    }
}

auto handleGetAssociation(sdbus::IProxy& proxy, std::uint8_t& callbackCounter) -> void
{
    auto nodeId = promptByte("Node ID (1-232):", NODE_ID_MIN, NODE_ID_MAX);
    if (!nodeId.has_value())
    {
        logLine("GetAssociation: cancelled or invalid node id");
        return;
    }
    auto groupId = promptByte("Group ID (1-255):", GROUP_ID_MIN, GROUP_ID_MAX);
    if (!groupId.has_value())
    {
        logLine("GetAssociation: cancelled or invalid group id");
        return;
    }
    ++callbackCounter;
    try
    {
        proxy.callMethod("GetAssociation").onInterface(IFACE_NAME).withArguments(*nodeId, *groupId, callbackCounter);
        std::ostringstream stream;
        stream << "GetAssociation node=" << static_cast<unsigned>(*nodeId)
               << " group=" << static_cast<unsigned>(*groupId)
               << " callback=" << static_cast<unsigned>(callbackCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetAssociation failed: "} + err.what());
    }
}

auto handleSetLifeline(sdbus::IProxy& proxy, std::uint8_t& callbackCounter) -> void
{
    auto nodeId = promptByte("Node ID (1-232):", NODE_ID_MIN, NODE_ID_MAX);
    if (!nodeId.has_value())
    {
        logLine("SetAssociation (lifeline): cancelled or invalid node id");
        return;
    }
    auto controllerNodeId = fetchControllerNodeId(proxy);
    if (!controllerNodeId.has_value())
    {
        logLine("SetAssociation (lifeline): controller node id unavailable (no DongleInfo yet)");
        return;
    }
    ++callbackCounter;
    const std::vector<std::uint8_t> members{*controllerNodeId};
    try
    {
        proxy.callMethod("SetAssociation")
            .onInterface(IFACE_NAME)
            .withArguments(*nodeId, LIFELINE_GROUP, members, callbackCounter);
        std::ostringstream stream;
        stream << "SetAssociation (lifeline) node=" << static_cast<unsigned>(*nodeId) << " group=1 members=["
               << static_cast<unsigned>(*controllerNodeId) << "] callback=" << static_cast<unsigned>(callbackCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetAssociation failed: "} + err.what());
    }
}

auto handleRemoveFailedNode(sdbus::IProxy& proxy, std::uint8_t& sessionCounter) -> void
{
    auto nodeId = promptByte("Failed node ID (1-232):", NODE_ID_MIN, NODE_ID_MAX);
    if (!nodeId.has_value())
    {
        logLine("RemoveFailedNode: cancelled or invalid node id");
        return;
    }
    ++sessionCounter;
    try
    {
        proxy.callMethod("RemoveFailedNode").onInterface(IFACE_NAME).withArguments(*nodeId, sessionCounter);
        std::ostringstream stream;
        stream << "RemoveFailedNode node=" << static_cast<unsigned>(*nodeId)
               << " session=" << static_cast<unsigned>(sessionCounter);
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"RemoveFailedNode failed: "} + err.what());
    }
}

auto handleListNodes(sdbus::IProxy& proxy) -> void
{
    using NodeTuple = sdbus::Struct<std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t, std::vector<std::uint8_t>>;
    std::vector<NodeTuple> nodes;
    try
    {
        proxy.callMethod("GetNodes").onInterface(IFACE_NAME).storeResultsTo(nodes);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetNodes failed: "} + err.what());
        return;
    }

    if (nodes.empty())
    {
        logLine("Node list: (empty)");
        return;
    }

    logLine("Node list (" + std::to_string(nodes.size()) + "):");
    for (const auto& tup : nodes)
    {
        const auto nodeId   = std::get<0>(tup);
        const auto basic    = std::get<1>(tup);
        const auto generic  = std::get<2>(tup);
        const auto specific = std::get<3>(tup);
        const auto& ccs     = std::get<4>(tup);

        std::ostringstream stream;
        stream << "  node=" << static_cast<unsigned>(nodeId) << " basic=0x" << std::hex << std::setw(2)
               << std::setfill('0') << static_cast<unsigned>(basic) << " generic=0x" << std::setw(2)
               << static_cast<unsigned>(generic) << " specific=0x" << std::setw(2) << static_cast<unsigned>(specific)
               << std::dec << " " << formatCcList(ccs);
        logLine(stream.str());

        // Auto-introspect Association on supporting nodes. The
        // AssociationGroupingsReport handler chains GetAssociation
        // for each group, so we just kick off the GROUPINGS GET here.
        if (nodeSupportsCc(ccs, CC_ASSOCIATION))
        {
            try
            {
                proxy.callMethod("GetAssociationGroupings")
                    .onInterface(IFACE_NAME)
                    .withArguments(nodeId, CALLBACK_ID_NONE);
            }
            catch (const sdbus::Error& err)
            {
                logLine(std::string{"auto GetAssociationGroupings failed: "} + err.what());
            }
        }
    }
}

auto handleViewEffectivePolicy(sdbus::IProxy& proxy) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("GetEffectivePolicy: cancelled or invalid node id");
        return;
    }
    std::vector<std::uint8_t> bytes;
    try
    {
        proxy.callMethod("GetEffectivePolicy").onInterface(IFACE_NAME).withArguments(*nodeId).storeResultsTo(bytes);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetEffectivePolicy failed: "} + err.what());
        return;
    }
    logPolicy("Effective policy node=" + std::to_string(static_cast<unsigned>(*nodeId)) + ":", bytes);
}

auto handleViewNodeOverride(sdbus::IProxy& proxy) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("GetNodeOverride: cancelled or invalid node id");
        return;
    }
    std::vector<std::uint8_t> bytes;
    try
    {
        proxy.callMethod("GetNodeOverride").onInterface(IFACE_NAME).withArguments(*nodeId).storeResultsTo(bytes);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetNodeOverride failed: "} + err.what());
        return;
    }
    logPolicy("Node override node=" + std::to_string(static_cast<unsigned>(*nodeId)) + ":", bytes);
}

auto handleDeleteNodeOverride(sdbus::IProxy& proxy) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("DeleteNodeOverride: cancelled or invalid node id");
        return;
    }
    try
    {
        proxy.callMethod("DeleteNodeOverride").onInterface(IFACE_NAME).withArguments(*nodeId);
        logLine("DeleteNodeOverride node=" + std::to_string(static_cast<unsigned>(*nodeId)));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"DeleteNodeOverride failed: "} + err.what());
    }
}

// Prompt for one policy entry: kind, then the kind-specific fields.
// Returns nullopt on cancel / invalid input at any step.

auto handleSetNodeOverrideEntry(sdbus::IProxy& proxy) -> void
{
    auto nodeId = promptNodeId("Node ID (1-232):");
    if (!nodeId.has_value())
    {
        logLine("SetNodeOverride: cancelled or invalid node id");
        return;
    }
    auto entry = promptPolicyEntry();
    if (!entry.has_value())
    {
        return;
    }
    std::vector<std::uint8_t> existing;
    try
    {
        proxy.callMethod("GetNodeOverride").onInterface(IFACE_NAME).withArguments(*nodeId).storeResultsTo(existing);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetNodeOverride: read-back failed: "} + err.what());
        return;
    }
    bool replaced = false;
    auto blob     = applyEntryToBlob(existing, *entry, replaced);
    if (!blob.has_value())
    {
        logLine("SetNodeOverride: existing override is undecodable — aborting to avoid overwriting it");
        return;
    }
    try
    {
        proxy.callMethod("SetNodeOverride").onInterface(IFACE_NAME).withArguments(*nodeId, *blob);
        logLine("SetNodeOverride node=" + std::to_string(static_cast<unsigned>(*nodeId)) + " " + entrySummary(*entry) +
                (replaced ? " (updated)" : " (added)"));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetNodeOverride failed: "} + err.what());
    }
}

// Device-policy authoring: set/update an entry or delete the whole policy
// for a (manufacturer, type, product) device, by the same edit-in-place
// flow as node overrides.
auto handleDevicePolicyEdit(sdbus::IProxy& proxy) -> void
{
    auto action = promptChar("Device policy: [s]et entry  [d]elete policy:", "sd");
    if (!action.has_value())
    {
        logLine("Device policy: cancelled");
        return;
    }
    auto manufacturerId = promptU16("Manufacturer id (dec or 0xHEX):");
    auto productTypeId  = promptU16("Product type id:");
    auto productId      = promptU16("Product id:");
    if (!manufacturerId.has_value() || !productTypeId.has_value() || !productId.has_value())
    {
        logLine("Device policy: cancelled or invalid device id");
        return;
    }

    if (*action == 'd')
    {
        try
        {
            proxy.callMethod("DeleteDevicePolicy")
                .onInterface(IFACE_NAME)
                .withArguments(*manufacturerId, *productTypeId, *productId);
            logLine("DeleteDevicePolicy mfr=" + std::to_string(*manufacturerId));
        }
        catch (const sdbus::Error& err)
        {
            logLine(std::string{"DeleteDevicePolicy failed: "} + err.what());
        }
        return;
    }

    auto entry = promptPolicyEntry();
    if (!entry.has_value())
    {
        return;
    }
    std::vector<std::uint8_t> existing;
    try
    {
        proxy.callMethod("GetDevicePolicy")
            .onInterface(IFACE_NAME)
            .withArguments(*manufacturerId, *productTypeId, *productId)
            .storeResultsTo(existing);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetDevicePolicy: read-back failed: "} + err.what());
        return;
    }
    bool replaced = false;
    auto blob     = applyEntryToBlob(existing, *entry, replaced);
    if (!blob.has_value())
    {
        logLine("SetDevicePolicy: existing policy is undecodable — aborting to avoid overwriting it");
        return;
    }
    try
    {
        proxy.callMethod("SetDevicePolicy")
            .onInterface(IFACE_NAME)
            .withArguments(*manufacturerId, *productTypeId, *productId, *blob);
        std::ostringstream stream;
        stream << "SetDevicePolicy mfr=0x" << std::hex << std::setw(4) << std::setfill('0') << *manufacturerId
               << std::dec << " " << entrySummary(*entry) << (replaced ? " (updated)" : " (added)");
        logLine(stream.str());
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"SetDevicePolicy failed: "} + err.what());
    }
}

auto handleListDevicePolicies(sdbus::IProxy& proxy) -> void
{
    using DevicePolicyTuple = sdbus::Struct<std::uint16_t, std::uint16_t, std::uint16_t, std::vector<std::uint8_t>>;
    std::vector<DevicePolicyTuple> rows;
    try
    {
        proxy.callMethod("ListDevicePolicies").onInterface(IFACE_NAME).storeResultsTo(rows);
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"ListDevicePolicies failed: "} + err.what());
        return;
    }
    if (rows.empty())
    {
        logLine("Device policies: (none)");
        return;
    }
    logLine("Device policies (" + std::to_string(rows.size()) + "):");
    for (const auto& row : rows)
    {
        std::ostringstream header;
        header << "  device mfr=0x" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<unsigned>(std::get<0>(row)) << " type=0x" << std::setw(4)
               << static_cast<unsigned>(std::get<1>(row)) << " id=0x" << std::setw(4)
               << static_cast<unsigned>(std::get<2>(row)) << std::dec << ":";
        logPolicy(header.str(), std::get<3>(row));
    }
}

// NOLINTBEGIN(readability-function-cognitive-complexity): flat key-dispatch table
}  // namespace zwt
