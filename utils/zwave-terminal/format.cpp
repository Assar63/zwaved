#include "format.hpp"

#include "constants.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>
#include <vector>

namespace zwt
{
auto formatNetworkStatus(std::uint8_t status) -> const char*
{
    switch (status)
    {
    case STATUS_STARTED:
        return "Started";
    case STATUS_NODE_FOUND:
        return "Node found";
    case STATUS_ONGOING_END:
        return "Ongoing - End Node";
    case STATUS_ONGOING_CTRL:
        return "Ongoing - Controller";
    case STATUS_PROTOCOL_DONE:
        return "Protocol complete";
    case STATUS_COMPLETED:
        return "Completed";
    case STATUS_FAILED:
        return "Failed";
    case STATUS_NEIGHBORS_DONE:
        return "Neighbors discovery done";
    case STATUS_NOT_PRIMARY:
        return "Not primary";
    default:
        return "?";
    }
}

auto formatStatusEntry(const char* operation,
                       std::uint8_t sessionId,
                       std::uint8_t status,
                       std::uint16_t nodeId) -> std::string
{
    std::ostringstream stream;
    stream << operation << " session=" << static_cast<unsigned>(sessionId) << " status=0x" << std::hex << std::setw(2)
           << std::setfill('0') << static_cast<unsigned>(status) << " (" << formatNetworkStatus(status) << ")"
           << std::dec << " node=" << static_cast<unsigned>(nodeId);
    return stream.str();
}

auto formatTxStatus(std::uint8_t status) -> const char*
{
    switch (status)
    {
    case TX_STATUS_OK:
        return "OK";
    case TX_STATUS_NO_ACK:
        return "No ACK";
    case TX_STATUS_FAIL:
        return "Failed";
    case TX_STATUS_NOT_IDLE:
        return "Routing not idle";
    case TX_STATUS_NO_ROUTE:
        return "No route";
    case TX_STATUS_VERIFIED:
        return "Verified";
    default:
        return "?";
    }
}

auto formatSwitchState(std::uint8_t state) -> const char*
{
    switch (state)
    {
    case SWITCH_STATE_OFF:
        return "Off";
    case SWITCH_STATE_ON:
        return "On";
    case SWITCH_STATE_UNKNOWN:
        return "Unknown";
    default:
        return "?";
    }
}

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers): Z-Wave sensor-type IDs from the AWG spec
auto sensorTypeName(std::uint8_t sensorType) -> const char*
{
    switch (sensorType)
    {
    case 0x01:
        return "Air temperature";
    case 0x03:
        return "Luminance";
    case 0x04:
        return "Power";
    case 0x05:
        return "Humidity";
    case 0x11:
        return "Moisture";
    case 0x1B:
        return "Ultraviolet";
    default:
        return nullptr;
    }
}

// Unit string for a (sensorType, scale) pair; empty when unknown.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): both are wire fields, named at the call site
auto sensorUnit(std::uint8_t sensorType, std::uint8_t scale) -> const char*
{
    switch (sensorType)
    {
    case 0x01:  // air temperature
        return scale == 0 ? "C" : "F";
    case 0x03:  // luminance
        return scale == 0 ? "%" : "lux";
    case 0x04:  // power
        return scale == 0 ? "W" : "BTU/h";
    case 0x05:  // humidity
        return scale == 0 ? "%" : "g/m3";
    default:
        return "";
    }
}

// Meter (CC 0x32) type name; nullptr when unknown.
auto meterTypeName(std::uint8_t meterType) -> const char*
{
    switch (meterType)
    {
    case 0x01:
        return "electric";
    case 0x02:
        return "gas";
    case 0x03:
        return "water";
    default:
        return nullptr;
    }
}

// Unit string for a (meterType, scale) pair; empty when unknown.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): both are wire fields, named at the call site
auto meterUnit(std::uint8_t meterType, std::uint8_t scale) -> const char*
{
    switch (meterType)
    {
    case 0x01:  // electric
        switch (scale)
        {
        case 0:
            return "kWh";
        case 1:
            return "kVAh";
        case 2:
            return "W";
        case 4:
            return "V";
        case 5:
            return "A";
        default:
            return "";
        }
    case 0x02:  // gas
    case 0x03:  // water
        return scale == 0 ? "m3" : "";
    default:
        return "";
    }
}

// Thermostat Mode (CC 0x40) name; nullptr when unknown.
auto thermostatModeName(std::uint8_t mode) -> const char*
{
    switch (mode)
    {
    case 0:
        return "off";
    case 1:
        return "heat";
    case 2:
        return "cool";
    case 3:
        return "auto";
    case 4:
        return "aux heat";
    case 6:
        return "fan only";
    case 8:
        return "dry";
    case 10:
        return "auto changeover";
    case 11:
        return "energy-save heat";
    case 12:
        return "energy-save cool";
    case 13:
        return "away";
    default:
        return nullptr;
    }
}

// Thermostat Operating State (CC 0x42) name; nullptr when unknown.
auto thermostatOperatingStateName(std::uint8_t state) -> const char*
{
    switch (state)
    {
    case 0:
        return "idle";
    case 1:
        return "heating";
    case 2:
        return "cooling";
    case 3:
        return "fan only";
    case 4:
        return "pending heat";
    case 5:
        return "pending cool";
    case 6:
        return "vent/economizer";
    default:
        return nullptr;
    }
}

// Thermostat Fan Mode (CC 0x44) name; nullptr when unknown.
auto thermostatFanModeName(std::uint8_t mode) -> const char*
{
    switch (mode)
    {
    case 0:
        return "auto low";
    case 1:
        return "low";
    case 2:
        return "auto high";
    case 3:
        return "high";
    case 4:
        return "auto medium";
    case 5:
        return "medium";
    default:
        return nullptr;
    }
}

// Color Switch (CC 0x33) component name; nullptr when unknown.
auto colorComponentName(std::uint8_t componentId) -> const char*
{
    switch (componentId)
    {
    case 0:
        return "warm white";
    case 1:
        return "cold white";
    case 2:
        return "red";
    case 3:
        return "green";
    case 4:
        return "blue";
    case 5:
        return "amber";
    case 6:
        return "cyan";
    case 7:
        return "purple";
    default:
        return nullptr;
    }
}

// Central Scene (CC 0x5B) key-attribute name; nullptr when unknown.
auto centralSceneKeyName(std::uint8_t keyAttribute) -> const char*
{
    switch (keyAttribute)
    {
    case 0:
        return "press 1x";
    case 1:
        return "release";
    case 2:
        return "hold";
    case 3:
        return "press 2x";
    case 4:
        return "press 3x";
    case 5:
        return "press 4x";
    case 6:
        return "press 5x";
    default:
        return nullptr;
    }
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

/// Z-Wave Command Class human-readable names. Covers the most commonly
/// seen CCs from the AWG specification; unknown values render as bare
/// hex. Order isn't significant — the lookup is linear (~50 entries).
struct CcName
{
    std::uint8_t id;
    const char* name;
};
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers): Z-Wave CC IDs from the AWG spec
constexpr auto CC_NAMES = std::to_array<CcName>({
    {.id = 0x20, .name = "Basic"},
    {.id = 0x22, .name = "ApplicationStatus"},
    {.id = 0x25, .name = "SwitchBinary"},
    {.id = 0x26, .name = "SwitchMultilevel"},
    {.id = 0x27, .name = "SwitchAll"},
    {.id = 0x2B, .name = "SceneActivation"},
    {.id = 0x2C, .name = "SceneActuatorConf"},
    {.id = 0x2D, .name = "SceneControllerConf"},
    {.id = 0x30, .name = "SensorBinary"},
    {.id = 0x31, .name = "SensorMultilevel"},
    {.id = 0x32, .name = "Meter"},
    {.id = 0x33, .name = "ColorSwitch"},
    {.id = 0x40, .name = "ThermostatMode"},
    {.id = 0x42, .name = "ThermostatOperatingState"},
    {.id = 0x43, .name = "ThermostatSetpoint"},
    {.id = 0x44, .name = "ThermostatFanMode"},
    {.id = 0x45, .name = "ThermostatFanState"},
    {.id = 0x55, .name = "TransportService"},
    {.id = 0x56, .name = "Crc16Encap"},
    {.id = 0x59, .name = "AssociationGrpInfo"},
    {.id = 0x5A, .name = "DeviceResetLocally"},
    {.id = 0x5B, .name = "CentralScene"},
    {.id = 0x5E, .name = "ZwavePlusInfo"},
    {.id = 0x60, .name = "MultiChannel"},
    {.id = 0x62, .name = "DoorLock"},
    {.id = 0x63, .name = "UserCode"},
    {.id = 0x6C, .name = "Supervision"},
    {.id = 0x70, .name = "Configuration"},
    {.id = 0x71, .name = "Notification"},
    {.id = 0x72, .name = "ManufacturerSpecific"},
    {.id = 0x73, .name = "Powerlevel"},
    {.id = 0x75, .name = "Protection"},
    {.id = 0x77, .name = "NodeNaming"},
    {.id = 0x7A, .name = "FirmwareUpdateMd"},
    {.id = 0x80, .name = "Battery"},
    {.id = 0x81, .name = "Clock"},
    {.id = 0x82, .name = "Hail"},
    {.id = 0x84, .name = "WakeUp"},
    {.id = 0x85, .name = "Association"},
    {.id = 0x86, .name = "Version"},
    {.id = 0x87, .name = "Indicator"},
    {.id = 0x8E, .name = "MultiChannelAssociation"},
    {.id = 0x8F, .name = "MultiCmd"},
    {.id = 0x98, .name = "Security"},
    {.id = 0x9F, .name = "Security2"},
});
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

auto commandClassName(std::uint8_t commandClass) -> const char*
{
    for (const auto& [id, name] : CC_NAMES)
    {
        if (id == commandClass)
        {
            return name;
        }
    }
    return nullptr;
}

auto formatCcRange(std::vector<std::uint8_t>::const_iterator begin,
                   std::vector<std::uint8_t>::const_iterator end) -> std::string
{
    std::ostringstream stream;
    stream << "[";
    bool first = true;
    for (auto iter = begin; iter != end; ++iter)
    {
        if (!first)
        {
            stream << " ";
        }
        first = false;
        stream << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(*iter) << std::dec;
        if (const auto* name = commandClassName(*iter); name != nullptr)
        {
            stream << "(" << name << ")";
        }
    }
    stream << "]";
    return stream.str();
}

/// Render a node's CC list, splitting on COMMAND_CLASS_MARK (0xEF) into
/// the supported CCs (responds to) and the controlled CCs (emits to
/// associated nodes). The mark is omitted from either side. If the
/// node advertises no controlled CCs, only "supports=…" is shown.
auto formatCcList(const std::vector<std::uint8_t>& ccs) -> std::string
{
    const auto mark = std::find(ccs.begin(), ccs.end(), CC_MARK);
    if (mark == ccs.end())
    {
        return "supports=" + formatCcRange(ccs.begin(), ccs.end());
    }
    return "supports=" + formatCcRange(ccs.begin(), mark) + " controls=" + formatCcRange(mark + 1, ccs.end());
}
}  // namespace zwt
