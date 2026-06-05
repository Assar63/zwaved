#include "signal_handlers.hpp"

#include "activity.hpp"
#include "constants.hpp"
#include "format.hpp"

#include <cstdint>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>
#include <vector>

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IProxy.h>

namespace zwt
{
// NOLINTBEGIN(readability-function-cognitive-complexity): flat list of signal subscriptions
auto registerSignalHandlers(sdbus::IProxy& proxy) -> void
{
    proxy.uponSignal("NodeInclusionStatus")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sessionId,
               std::uint8_t status,
               std::uint16_t nodeId,
               std::uint8_t /*basic*/,
               std::uint8_t /*generic*/,
               std::uint8_t /*specific*/,
               const std::vector<std::uint8_t>& /*ccs*/) -> void
            { logLine(formatStatusEntry("Inclusion", sessionId, status, nodeId)); });

    proxy.uponSignal("NodeExclusionStatus")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sessionId,
               std::uint8_t status,
               std::uint16_t nodeId,
               std::uint8_t /*basic*/,
               std::uint8_t /*generic*/,
               std::uint8_t /*specific*/,
               const std::vector<std::uint8_t>& /*ccs*/) -> void
            { logLine(formatStatusEntry("Exclusion", sessionId, status, nodeId)); });

    proxy.uponSignal("DongleStatus")
        .onInterface(IFACE_NAME)
        .call(
            [](bool connected, const std::string& path) -> void
            {
                setDongleStatus(connected, path);
                logLine(connected ? "DongleStatus: connected " + path : "DongleStatus: disconnected");
            });

    proxy.uponSignal("SendDataStatus")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t callbackId, std::uint8_t txStatus) -> void
            {
                std::ostringstream stream;
                stream << "SendDataStatus callback=" << static_cast<unsigned>(callbackId) << " status=0x" << std::hex
                       << std::setw(2) << std::setfill('0') << static_cast<unsigned>(txStatus) << " ("
                       << formatTxStatus(txStatus) << ")";
                logLine(stream.str());
            });

    proxy.uponSignal("RemoveFailedNodeStatus")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t nodeId, std::uint8_t sessionId, std::uint8_t phase, std::uint8_t status) -> void
            {
                std::ostringstream stream;
                stream << "RemoveFailedNodeStatus node=" << static_cast<unsigned>(nodeId)
                       << " session=" << static_cast<unsigned>(sessionId) << (phase == 0 ? " response=" : " result=")
                       << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(status);
                logLine(stream.str());
            });

    proxy.uponSignal("SwitchBinaryReport")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId, std::uint8_t state) -> void
            {
                std::ostringstream stream;
                stream << "SwitchBinaryReport node=" << static_cast<unsigned>(sourceNodeId)
                       << " state=" << formatSwitchState(state);
                logLine(stream.str());
            });

    proxy.uponSignal("SwitchMultilevelReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId, std::uint8_t currentValue, std::uint8_t targetValue, std::uint8_t duration)
                -> void
            {
                std::ostringstream stream;
                stream << "SwitchMultilevelReport node=" << static_cast<unsigned>(sourceNodeId)
                       << " current=" << static_cast<unsigned>(currentValue)
                       << " target=" << static_cast<unsigned>(targetValue) << " duration=0x" << std::hex << std::setw(2)
                       << std::setfill('0') << static_cast<unsigned>(duration) << std::dec;
                logLine(stream.str());
            });

    // NOLINTBEGIN(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
    proxy.uponSignal("ColorSwitchReport")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId,
               std::uint8_t componentId,
               std::uint8_t value,
               std::uint8_t targetValue,
               std::uint8_t duration) -> void
            {
                std::ostringstream stream;
                stream << "ColorSwitchReport node=" << static_cast<unsigned>(sourceNodeId) << " ";
                if (const char* name = colorComponentName(componentId); name != nullptr)
                {
                    stream << name;
                }
                else
                {
                    stream << "component=" << static_cast<unsigned>(componentId);
                }
                stream << "=" << static_cast<unsigned>(value);
                if (targetValue != value || duration != 0)
                {
                    stream << " (target " << static_cast<unsigned>(targetValue) << ", dur "
                           << static_cast<unsigned>(duration) << ")";
                }
                logLine(stream.str());
            });

    proxy.uponSignal("CentralSceneNotification")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId,
               std::uint8_t sequenceNumber,
               std::uint8_t keyAttribute,
               std::uint8_t sceneNumber,
               bool slowRefresh) -> void
            {
                std::ostringstream stream;
                stream << "CentralSceneNotification node=" << static_cast<unsigned>(sourceNodeId)
                       << " scene=" << static_cast<unsigned>(sceneNumber) << " ";
                if (const char* name = centralSceneKeyName(keyAttribute); name != nullptr)
                {
                    stream << name;
                }
                else
                {
                    stream << "key=" << static_cast<unsigned>(keyAttribute);
                }
                stream << " seq=" << static_cast<unsigned>(sequenceNumber);
                if (slowRefresh)
                {
                    stream << " (slow-refresh)";
                }
                logLine(stream.str());
            });

    proxy.uponSignal("DoorLockOperationReport")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId,
               std::uint8_t currentMode,
               std::uint8_t /*handlesMode*/,
               std::uint8_t condition,
               std::uint8_t lockTimeoutMinutes,
               std::uint8_t lockTimeoutSeconds,
               std::uint8_t /*targetMode*/,
               std::uint8_t /*duration*/) -> void
            {
                std::ostringstream stream;
                stream << "DoorLockOperationReport node=" << static_cast<unsigned>(sourceNodeId) << " mode=0x"
                       << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(currentMode)
                       << " condition=0x" << std::setw(2) << static_cast<unsigned>(condition) << std::dec
                       << " timeout=" << static_cast<unsigned>(lockTimeoutMinutes) << "m"
                       << static_cast<unsigned>(lockTimeoutSeconds) << "s";
                logLine(stream.str());
            });

    proxy.uponSignal("UserCodeReport")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId,
               std::uint8_t userIdentifier,
               std::uint8_t userIdStatus,
               const std::vector<std::uint8_t>& userCode) -> void
            {
                std::ostringstream stream;
                stream << "UserCodeReport node=" << static_cast<unsigned>(sourceNodeId)
                       << " slot=" << static_cast<unsigned>(userIdentifier) << " status=0x" << std::hex << std::setw(2)
                       << std::setfill('0') << static_cast<unsigned>(userIdStatus) << std::dec
                       << " codeLen=" << userCode.size();
                logLine(stream.str());
            });

    proxy.uponSignal("UserCodeUsersNumberReport")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId, std::uint8_t supportedUsers) -> void
            {
                logLine("UserCodeUsersNumberReport node=" + std::to_string(static_cast<unsigned>(sourceNodeId)) +
                        " slots=" + std::to_string(static_cast<unsigned>(supportedUsers)));
            });
    // NOLINTEND(bugprone-easily-swappable-parameters)

    proxy.uponSignal("BatteryReport")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId, std::uint8_t level, bool lowBattery) -> void
            {
                std::ostringstream stream;
                stream << "BatteryReport node=" << static_cast<unsigned>(sourceNodeId) << " level=";
                if (level == BYTE_MAX)
                {
                    stream << "low(0xFF)";
                }
                else
                {
                    stream << static_cast<unsigned>(level) << "%";
                }
                if (lowBattery)
                {
                    stream << " [LOW]";
                }
                logLine(stream.str());
            });

    // NOLINTBEGIN(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
    proxy.uponSignal("SensorMultilevelReport")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId,
               std::uint8_t sensorType,
               std::uint8_t scale,
               std::uint8_t precision,
               std::int32_t value) -> void
            {
                // reading = value / 10^precision, with `precision` decimals.
                int divisor = 1;
                for (std::uint8_t i = 0; i < precision; ++i)
                {
                    divisor *= DECIMAL_BASE;
                }
                std::ostringstream stream;
                stream << "SensorMultilevelReport node=" << static_cast<unsigned>(sourceNodeId) << " ";
                if (const char* name = sensorTypeName(sensorType); name != nullptr)
                {
                    stream << name;
                }
                else
                {
                    stream << "type=0x" << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<unsigned>(sensorType) << std::dec;
                }
                stream << "=" << std::fixed << std::setprecision(precision) << static_cast<double>(value) / divisor;
                if (const char* unit = sensorUnit(sensorType, scale); *unit != '\0')
                {
                    stream << " " << unit;
                }
                logLine(stream.str());
            });
    // NOLINTEND(bugprone-easily-swappable-parameters)

    proxy.uponSignal("SensorBinaryReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId, std::uint8_t sensorType, std::uint8_t value) -> void
            {
                std::ostringstream stream;
                stream << "SensorBinaryReport node=" << static_cast<unsigned>(sourceNodeId);
                if (sensorType != 0)
                {
                    stream << " type=0x" << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<unsigned>(sensorType) << std::dec;
                }
                stream << (value != 0 ? " active" : " idle");
                logLine(stream.str());
            });

    proxy.uponSignal("NotificationReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId,
               std::uint8_t notificationType,
               std::uint8_t event,
               std::uint8_t status,
               const std::vector<std::uint8_t>& parameters) -> void
            {
                std::ostringstream stream;
                stream << "NotificationReport node=" << static_cast<unsigned>(sourceNodeId) << std::hex
                       << std::setfill('0') << " type=0x" << std::setw(2) << static_cast<unsigned>(notificationType)
                       << " event=0x" << std::setw(2) << static_cast<unsigned>(event) << " status=0x" << std::setw(2)
                       << static_cast<unsigned>(status);
                if (!parameters.empty())
                {
                    stream << " params=[";
                    bool first = true;
                    for (const auto byte : parameters)
                    {
                        if (!first)
                        {
                            stream << " ";
                        }
                        first = false;
                        stream << std::setw(2) << static_cast<unsigned>(byte);
                    }
                    stream << "]";
                }
                stream << std::dec;
                logLine(stream.str());
            });

    // NOLINTBEGIN(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
    proxy.uponSignal("MeterReport")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId,
               std::uint8_t meterType,
               std::uint8_t rateType,
               std::uint8_t scale,
               std::uint8_t precision,
               std::int32_t value,
               std::uint16_t deltaTime,
               std::int32_t previousValue,
               bool hasPrevious) -> void
            {
                // reading = value / 10^precision, with `precision` decimals.
                int divisor = 1;
                for (std::uint8_t i = 0; i < precision; ++i)
                {
                    divisor *= DECIMAL_BASE;
                }
                std::ostringstream stream;
                stream << "MeterReport node=" << static_cast<unsigned>(sourceNodeId) << " ";
                if (const char* name = meterTypeName(meterType); name != nullptr)
                {
                    stream << name;
                }
                else
                {
                    stream << "type=0x" << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<unsigned>(meterType) << std::dec;
                }
                if (rateType == 2)
                {
                    stream << " (export)";
                }
                stream << " " << std::fixed << std::setprecision(precision) << static_cast<double>(value) / divisor;
                if (const char* unit = meterUnit(meterType, scale); *unit != '\0')
                {
                    stream << " " << unit;
                }
                if (hasPrevious)
                {
                    stream << " (Δ" << static_cast<unsigned>(deltaTime) << "s, prev "
                           << static_cast<double>(previousValue) / divisor << ")";
                }
                logLine(stream.str());
            });
    // NOLINTEND(bugprone-easily-swappable-parameters)

    proxy.uponSignal("ThermostatModeReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId, std::uint8_t mode) -> void
            {
                std::ostringstream stream;
                stream << "ThermostatModeReport node=" << static_cast<unsigned>(sourceNodeId) << " mode=";
                if (const char* name = thermostatModeName(mode); name != nullptr)
                {
                    stream << name;
                }
                else
                {
                    stream << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(mode)
                           << std::dec;
                }
                logLine(stream.str());
            });

    proxy.uponSignal("ThermostatOperatingStateReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId, std::uint8_t state) -> void
            {
                std::ostringstream stream;
                stream << "ThermostatOperatingStateReport node=" << static_cast<unsigned>(sourceNodeId) << " state=";
                if (const char* name = thermostatOperatingStateName(state); name != nullptr)
                {
                    stream << name;
                }
                else
                {
                    stream << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(state)
                           << std::dec;
                }
                logLine(stream.str());
            });

    proxy.uponSignal("ThermostatFanModeReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId, std::uint8_t mode, bool off) -> void
            {
                std::ostringstream stream;
                stream << "ThermostatFanModeReport node=" << static_cast<unsigned>(sourceNodeId) << " mode=";
                if (const char* name = thermostatFanModeName(mode); name != nullptr)
                {
                    stream << name;
                }
                else
                {
                    stream << "0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(mode)
                           << std::dec;
                }
                if (off)
                {
                    stream << " (fan off)";
                }
                logLine(stream.str());
            });

    proxy.uponSignal("ThermostatSetpointReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId,
               std::uint8_t setpointType,
               std::uint8_t scale,
               std::uint8_t precision,
               std::int32_t value) -> void
            {
                int divisor = 1;
                for (std::uint8_t i = 0; i < precision; ++i)
                {
                    divisor *= DECIMAL_BASE;
                }
                std::ostringstream stream;
                stream << "ThermostatSetpointReport node=" << static_cast<unsigned>(sourceNodeId)
                       << " type=" << static_cast<unsigned>(setpointType) << " " << std::fixed
                       << std::setprecision(precision) << static_cast<double>(value) / divisor
                       << (scale == 0 ? " C" : " F");
                logLine(stream.str());
            });

    proxy.uponSignal("ConfigurationReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId, std::uint8_t parameter, std::uint8_t size, std::int32_t value) -> void
            {
                std::ostringstream stream;
                stream << "ConfigurationReport node=" << static_cast<unsigned>(sourceNodeId)
                       << " param=" << static_cast<unsigned>(parameter) << " size=" << static_cast<unsigned>(size)
                       << " value=" << value;
                logLine(stream.str());
            });

    proxy.uponSignal("ManufacturerSpecificReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId,
               std::uint16_t manufacturerId,
               std::uint16_t productTypeId,
               std::uint16_t productId) -> void
            {
                std::ostringstream stream;
                stream << "ManufacturerSpecificReport node=" << static_cast<unsigned>(sourceNodeId) << std::hex
                       << std::setfill('0') << " mfr=0x" << std::setw(4) << manufacturerId << " type=0x" << std::setw(4)
                       << productTypeId << " product=0x" << std::setw(4) << productId << std::dec;
                logLine(stream.str());
            });

    proxy.uponSignal("NodeVersionReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId,
               std::uint8_t libraryType,
               std::uint8_t protocolVersion,
               std::uint8_t protocolSubVersion,
               std::uint8_t applicationVersion,
               std::uint8_t applicationSubVersion) -> void
            {
                std::ostringstream stream;
                stream << "NodeVersionReport node=" << static_cast<unsigned>(sourceNodeId)
                       << " lib=" << static_cast<unsigned>(libraryType)
                       << " proto=" << static_cast<unsigned>(protocolVersion) << "."
                       << static_cast<unsigned>(protocolSubVersion)
                       << " app=" << static_cast<unsigned>(applicationVersion) << "."
                       << static_cast<unsigned>(applicationSubVersion);
                logLine(stream.str());
            });

    proxy.uponSignal("ZWavePlusInfoReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId,
               std::uint8_t zwavePlusVersion,
               std::uint8_t roleType,
               std::uint8_t nodeType,
               std::uint16_t installerIconType,
               std::uint16_t userIconType) -> void
            {
                std::ostringstream stream;
                stream << "ZWavePlusInfoReport node=" << static_cast<unsigned>(sourceNodeId)
                       << " ver=" << static_cast<unsigned>(zwavePlusVersion)
                       << " role=" << static_cast<unsigned>(roleType) << " nodeType=" << static_cast<unsigned>(nodeType)
                       << std::hex << std::setfill('0') << " icons=0x" << std::setw(4) << installerIconType << "/0x"
                       << std::setw(4) << userIconType << std::dec;
                logLine(stream.str());
            });

    proxy.uponSignal("WakeUpIntervalReport")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId, std::uint32_t seconds, std::uint8_t controllerNodeId) -> void
            {
                std::ostringstream stream;
                stream << "WakeUpIntervalReport node=" << static_cast<unsigned>(sourceNodeId) << " interval=" << seconds
                       << "s notify=" << static_cast<unsigned>(controllerNodeId);
                logLine(stream.str());
            });

    proxy.uponSignal("WakeUpNotification")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t sourceNodeId) -> void {
                logLine("WakeUpNotification node=" + std::to_string(static_cast<unsigned>(sourceNodeId)) + " (awake)");
            });

    // Pending-command queue + wake-up orchestration traffic (#75): the
    // daemon stashes commands for sleeping nodes and drains them on
    // wake-up. These are observability signals only — the queue is fed
    // indirectly by Set/Get calls, not driven from here.
    proxy.uponSignal("PendingCommandEnqueued")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t nodeId, std::uint32_t sequence, std::uint8_t priority) -> void
            {
                std::ostringstream stream;
                stream << "PendingCommandEnqueued node=" << static_cast<unsigned>(nodeId) << " seq=" << sequence
                       << " priority=" << static_cast<unsigned>(priority);
                logLine(stream.str());
            });

    proxy.uponSignal("PendingCommandsDrained")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t nodeId, std::uint32_t count) -> void
            {
                std::ostringstream stream;
                stream << "PendingCommandsDrained node=" << static_cast<unsigned>(nodeId) << " count=" << count;
                logLine(stream.str());
            });

    proxy.uponSignal("WakeUpCycleComplete")
        .onInterface(IFACE_NAME)
        .call(
            [](std::uint8_t nodeId, std::uint32_t drainedCount) -> void
            {
                std::ostringstream stream;
                stream << "WakeUpCycleComplete node=" << static_cast<unsigned>(nodeId) << " drained=" << drainedCount
                       << " (back to sleep)";
                logLine(stream.str());
            });

    proxy.uponSignal("ApplicationCommand")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t /*rxStatus*/, std::uint8_t sourceNodeId, const std::vector<std::uint8_t>& ccData) -> void
            {
                // Surface unsolicited on/off events sent by binary-switch
                // nodes. Wall switches typically push Basic SET to their
                // lifeline association group on toggle; some devices send
                // SwitchBinary SET for the same purpose. SwitchBinary REPORT
                // (cmd 0x03) is handled by the typed SwitchBinaryReport
                // signal — skipped here to avoid duplicate log lines.
                if (ccData.size() < 3)
                {
                    return;
                }
                const auto commandClass = ccData.at(0);
                const auto command      = ccData.at(1);
                const auto value        = ccData.at(2);

                const char* origin = nullptr;
                if (commandClass == CC_BASIC && command == CMD_SET)
                {
                    origin = "Basic Set";
                }
                else if (commandClass == CC_BASIC && command == CMD_REPORT)
                {
                    origin = "Basic Report";
                }
                else if (commandClass == CC_SWITCH_BINARY && command == CMD_SET)
                {
                    origin = "SwitchBinary Set";
                }
                if (origin == nullptr)
                {
                    return;
                }
                const char* state = "On";
                if (value == WIRE_VALUE_OFF)
                {
                    state = "Off";
                }
                else if (value == WIRE_VALUE_UNKNOWN)
                {
                    state = "Unknown";
                }
                std::ostringstream stream;
                stream << origin << " node=" << static_cast<unsigned>(sourceNodeId) << " state=" << state;
                logLine(stream.str());
            });

    proxy.uponSignal("AssociationReport")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t sourceNodeId,
               std::uint8_t groupId,
               std::uint8_t maxSupported,
               std::uint8_t reportsToFollow,
               const std::vector<std::uint8_t>& members) -> void
            {
                std::ostringstream stream;
                stream << "AssociationReport node=" << static_cast<unsigned>(sourceNodeId)
                       << " group=" << static_cast<unsigned>(groupId) << " max=" << static_cast<unsigned>(maxSupported)
                       << " toFollow=" << static_cast<unsigned>(reportsToFollow) << " members=[";
                bool first = true;
                for (const auto member : members)
                {
                    if (!first)
                    {
                        stream << " ";
                    }
                    first = false;
                    stream << static_cast<unsigned>(member);
                }
                stream << "]";
                logLine(stream.str());
            });

    proxy.uponSignal("AssociationGroupingsReport")
        .onInterface(IFACE_NAME)
        .call(
            // Auto-chains a GetAssociation for each group when a groupings
            // report arrives, so [l] introspection (and manual [g]) end up
            // showing each group's members without further keystrokes.
            [&proxy](std::uint8_t sourceNodeId, std::uint8_t supportedGroupings) -> void
            {
                std::ostringstream stream;
                stream << "AssociationGroupingsReport node=" << static_cast<unsigned>(sourceNodeId)
                       << " groupings=" << static_cast<unsigned>(supportedGroupings);
                logLine(stream.str());
                for (std::uint8_t group = 1; group <= supportedGroupings; ++group)
                {
                    try
                    {
                        proxy.callMethod("GetAssociation")
                            .onInterface(IFACE_NAME)
                            .withArguments(sourceNodeId, group, CALLBACK_ID_NONE);
                    }
                    catch (const sdbus::Error& err)
                    {
                        logLine(std::string{"auto GetAssociation failed: "} + err.what());
                        break;
                    }
                }
            });

    proxy.uponSignal("InitData")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t serialApiVersion,
               std::uint8_t capabilities,
               const std::vector<std::uint8_t>& nodeIds,
               std::uint8_t chipType,
               std::uint8_t chipVersion) -> void
            {
                std::ostringstream stream;
                stream << "InitData: serialApiVersion=" << static_cast<unsigned>(serialApiVersion) << " capabilities=0x"
                       << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(capabilities)
                       << std::dec << " chipType=" << static_cast<unsigned>(chipType)
                       << " chipVer=" << static_cast<unsigned>(chipVersion) << " nodes=" << nodeIds.size();
                logLine(stream.str());
            });

    proxy.uponSignal("DongleInfo")
        .onInterface(IFACE_NAME)
        .call(
            [](const std::string& libraryVersion,
               std::uint8_t libraryType,
               const std::vector<std::uint8_t>& homeId,
               std::uint8_t controllerNodeId) -> void
            {
                std::ostringstream stream;
                stream << "DongleInfo: \"" << libraryVersion << "\" libType=" << static_cast<unsigned>(libraryType)
                       << " homeId=";
                for (const auto byte : homeId)
                {
                    stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
                }
                stream << std::dec << " controllerNode=" << static_cast<unsigned>(controllerNodeId);
                logLine(stream.str());
            });

    // Structured error feed (#76): drive the persistent banner. An empty
    // message means "recovered" and clears it. Retained on the daemon
    // side, so a terminal that connects after a failure picks up the
    // current value via GetDaemonError at startup (see main()).
    proxy.uponSignal("DaemonError")
        .onInterface(IFACE_NAME)
        .call(
            // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal
            [](std::uint8_t severity, const std::string& source, std::uint8_t code, const std::string& message) -> void
            {
                setDaemonError(severity, source, code, message);
                if (!message.empty())
                {
                    logLine("DaemonError [" + source + " code=" + std::to_string(static_cast<unsigned>(code)) +
                            "]: " + message);
                }
            });
}
// NOLINTEND(readability-function-cognitive-complexity)
}  // namespace zwt
