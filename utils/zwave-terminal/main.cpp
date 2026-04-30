#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <ncurses.h>
#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>

namespace
{
constexpr const char* BUS_NAME    = "com.tiunda.ZWaved";
constexpr const char* OBJECT_PATH = "/com/tiunda/ZWaved";
constexpr const char* IFACE_NAME  = "com.tiunda.ZWaved1";

constexpr int UI_REFRESH_MS                  = 100;
constexpr std::size_t MAX_LOG_LINES          = 200;
constexpr std::size_t TIMESTAMP_BUFFER_BYTES = 16;

constexpr std::uint8_t MODE_CLASSIC = 0x01;
constexpr std::uint8_t FLAGS_NONE   = 0x00;

// Inclusion / exclusion status codes — see MANUAL.md §9 and the Z-Wave spec
// (tables 4.124, 4.134).
constexpr std::uint8_t STATUS_STARTED        = 0x01;
constexpr std::uint8_t STATUS_NODE_FOUND     = 0x02;
constexpr std::uint8_t STATUS_ONGOING_END    = 0x03;
constexpr std::uint8_t STATUS_ONGOING_CTRL   = 0x04;
constexpr std::uint8_t STATUS_PROTOCOL_DONE  = 0x05;
constexpr std::uint8_t STATUS_COMPLETED      = 0x06;
constexpr std::uint8_t STATUS_FAILED         = 0x07;
constexpr std::uint8_t STATUS_NEIGHBORS_DONE = 0x0B;
constexpr std::uint8_t STATUS_NOT_PRIMARY    = 0x23;

// FUNC_ID_ZW_SEND_DATA callback transmit-status values (mirrors HostApi).
constexpr std::uint8_t TX_STATUS_OK       = 0x00;
constexpr std::uint8_t TX_STATUS_NO_ACK   = 0x01;
constexpr std::uint8_t TX_STATUS_FAIL     = 0x02;
constexpr std::uint8_t TX_STATUS_NOT_IDLE = 0x03;
constexpr std::uint8_t TX_STATUS_NO_ROUTE = 0x04;
constexpr std::uint8_t TX_STATUS_VERIFIED = 0x05;

// SwitchBinaryReport state encoding (matches BinarySwitch::State).
constexpr std::uint8_t SWITCH_STATE_OFF     = 0;
constexpr std::uint8_t SWITCH_STATE_ON      = 1;
constexpr std::uint8_t SWITCH_STATE_UNKNOWN = 2;

// Command-class wire constants for decoding unsolicited binary on/off
// traffic in ApplicationCommand frames.
constexpr std::uint8_t CC_BASIC           = 0x20;
constexpr std::uint8_t CC_SWITCH_BINARY   = 0x25;
constexpr std::uint8_t CMD_SET            = 0x01;
constexpr std::uint8_t CMD_REPORT         = 0x03;
constexpr std::uint8_t WIRE_VALUE_OFF     = 0x00;
constexpr std::uint8_t WIRE_VALUE_UNKNOWN = 0xFE;

// COMMAND_CLASS_MARK separates the CCs the node *supports* (i.e.
// will respond to) from the ones it *controls* (i.e. emits to its
// associated nodes — typically Basic SET on a wall switch toggle).
constexpr std::uint8_t CC_MARK        = 0xEF;
constexpr std::uint8_t CC_ASSOCIATION = 0x85;

// callbackId=0 in SendData means "no completion callback wanted from
// the dongle"; the node's application reply still arrives normally,
// so it's perfect for fire-and-forget auto-introspection queries.
constexpr std::uint8_t CALLBACK_ID_NONE = 0;

// Valid Z-Wave 8-bit node IDs (excluding broadcast 0 and reserved >232).
constexpr int NODE_ID_MIN = 1;
constexpr int NODE_ID_MAX = 232;

// Association group IDs are 1..255 per spec (0 reserved).
constexpr int GROUP_ID_MIN = 1;
constexpr int GROUP_ID_MAX = 255;

// Conventional Z-Wave lifeline association group.
constexpr std::uint8_t LIFELINE_GROUP = 1;

// Max characters of node-id input read from the bottom-row prompt
// (3 digits + null terminator, with slack).
constexpr std::size_t NODE_ID_INPUT_BUFFER = 8;

struct ActivityState
{
    std::mutex mutex;
    std::deque<std::string> log;
    bool dongleConnected{false};
    std::string donglePath;
};

auto activity() -> ActivityState&
{
    static ActivityState instance;
    return instance;
}

auto formatTimestamp() -> std::string
{
    const std::time_t epoch = std::time(nullptr);
    std::tm local{};
    ::localtime_r(&epoch, &local);
    std::array<char, TIMESTAMP_BUFFER_BYTES> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", &local);
    return {buffer.data()};
}

auto logLine(const std::string& message) -> void
{
    const std::string entry = formatTimestamp() + "  " + message;
    std::scoped_lock const lock(activity().mutex);
    activity().log.push_back(entry);
    while (activity().log.size() > MAX_LOG_LINES)
    {
        activity().log.pop_front();
    }
}

auto setDongleStatus(bool connected, const std::string& path) -> void
{
    std::scoped_lock const lock(activity().mutex);
    activity().dongleConnected = connected;
    activity().donglePath      = path;
}

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

/// Prompt at the bottom row for an integer in [minVal, maxVal]. Switches
/// ncurses to blocking echoing input, reads a line, parses it, then
/// restores the periodic-redraw input mode. Returns std::nullopt on
/// empty input, parse error, or out-of-range value.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): min/max are clearly named at call sites
auto promptByte(const char* label, int minVal, int maxVal) -> std::optional<std::uint8_t>
{
    const int rows = getmaxy(stdscr);
    move(rows - 1, 0);
    clrtoeol();
    mvprintw(rows - 1, 0, "%s ", label);
    refresh();

    echo();
    curs_set(1);
    timeout(-1);  // blocking

    std::array<char, NODE_ID_INPUT_BUFFER> buffer{};
    int const got = getnstr(buffer.data(), static_cast<int>(buffer.size()) - 1);

    noecho();
    curs_set(0);
    timeout(UI_REFRESH_MS);

    if (got != OK)
    {
        return std::nullopt;
    }

    const std::string text(buffer.data());
    if (text.empty())
    {
        return std::nullopt;
    }

    int value                   = 0;
    auto const [ptr, errorCode] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (errorCode != std::errc{} || value < minVal || value > maxVal)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

auto promptNodeId(const char* label) -> std::optional<std::uint8_t>
{
    return promptByte(label, NODE_ID_MIN, NODE_ID_MAX);
}

auto draw(std::uint8_t lastSession) -> void
{
    erase();
    int row = 0;

    {
        std::scoped_lock const lock(activity().mutex);
        const std::string status =
            activity().dongleConnected ? "connected (" + activity().donglePath + ")" : "disconnected";
        mvprintw(row++, 0, " zwave-terminal  -  Dongle: %s", status.c_str());
    }
    mvhline(row++, 0, '-', getmaxx(stdscr));

    mvprintw(row++, 0, "  [1] Add zwave node");
    mvprintw(row++, 0, "  [2] Remove zwave node");
    mvprintw(row++, 0, "  [3] Switch binary ON  (prompts for node id)");
    mvprintw(row++, 0, "  [4] Switch binary OFF (prompts for node id)");
    mvprintw(row++, 0, "  [a] Get association group members");
    mvprintw(row++, 0, "  [g] Get association group count");
    mvprintw(row++, 0, "  [L] Set lifeline (controller -> group 1)");
    mvprintw(row++, 0, "  [l] List included nodes");
    mvprintw(row++, 0, "  [i] Dongle info");
    mvprintw(row++, 0, "  [s] Stop current operation (session %u)", static_cast<unsigned>(lastSession));
    mvprintw(row++, 0, "  [q] Quit");
    mvhline(row++, 0, '-', getmaxx(stdscr));

    {
        std::scoped_lock const lock(activity().mutex);
        const auto& log              = activity().log;
        const int available          = getmaxy(stdscr) - row;
        const std::size_t startIndex = (available > 0 && log.size() > static_cast<std::size_t>(available))
                                           ? log.size() - static_cast<std::size_t>(available)
                                           : 0;
        for (std::size_t idx = startIndex; idx < log.size() && row < getmaxy(stdscr); ++idx)
        {
            mvprintw(row++, 0, "%s", log.at(idx).c_str());
        }
    }
    refresh();
}

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
}
// NOLINTEND(readability-function-cognitive-complexity)

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
    stream << "DongleInfo: \"" << libraryVersion << "\" libType=" << static_cast<unsigned>(libraryType) << " homeId=";
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
}  // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity): flat key-dispatch table
auto main() -> int
{
    std::unique_ptr<sdbus::IConnection> connection;
    std::unique_ptr<sdbus::IProxy> proxy;
    try
    {
        connection = sdbus::createSystemBusConnection();
        proxy      = sdbus::createProxy(*connection, BUS_NAME, OBJECT_PATH);
        registerSignalHandlers(*proxy);
        proxy->finishRegistration();
        connection->enterEventLoopAsync();
    }
    catch (const sdbus::Error& err)
    {
        std::cerr << "Failed to connect to " << BUS_NAME << ": " << err.what() << '\n';
        return 1;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(UI_REFRESH_MS);
    curs_set(0);

    std::uint8_t sessionCounter = 0;
    std::uint8_t lastSession    = 0;
    bool lastWasAdd             = false;
    bool running                = true;

    logLine(std::string{"Connected to "} + BUS_NAME);

    while (running)
    {
        draw(lastSession);
        const int key = getch();
        if (key == ERR)
        {
            continue;
        }

        try
        {
            if (key == 'q' || key == 'Q')
            {
                running = false;
            }
            else if (key == '1')
            {
                ++sessionCounter;
                const std::vector<std::uint8_t> empty;
                proxy->callMethod("AddNode")
                    .onInterface(IFACE_NAME)
                    .withArguments(MODE_CLASSIC, FLAGS_NONE, sessionCounter, empty, empty);
                lastSession = sessionCounter;
                lastWasAdd  = true;
                logLine("AddNode (classic, session " + std::to_string(static_cast<unsigned>(sessionCounter)) +
                        ") issued");
            }
            else if (key == '2')
            {
                ++sessionCounter;
                proxy->callMethod("RemoveNode")
                    .onInterface(IFACE_NAME)
                    .withArguments(MODE_CLASSIC, FLAGS_NONE, sessionCounter);
                lastSession = sessionCounter;
                lastWasAdd  = false;
                logLine("RemoveNode (classic, session " + std::to_string(static_cast<unsigned>(sessionCounter)) +
                        ") issued");
            }
            else if (key == '3' || key == '4')
            {
                handleSwitchBinary(*proxy, sessionCounter, key == '3');
            }
            else if (key == 'l' || key == 'L')
            {
                handleListNodes(*proxy);
            }
            else if (key == 'i' || key == 'I')
            {
                handleDongleInfo(*proxy);
            }
            else if (key == 'a')
            {
                handleGetAssociation(*proxy, sessionCounter);
            }
            else if (key == 'g' || key == 'G')
            {
                handleGetAssociationGroupings(*proxy, sessionCounter);
            }
            else if (key == 'L')
            {
                handleSetLifeline(*proxy, sessionCounter);
            }
            else if (key == 's' || key == 'S')
            {
                if (lastSession == 0)
                {
                    logLine("No session to stop");
                }
                else if (lastWasAdd)
                {
                    proxy->callMethod("StopAddNode").onInterface(IFACE_NAME).withArguments(lastSession);
                    logLine("StopAddNode session " + std::to_string(static_cast<unsigned>(lastSession)) + " issued");
                }
                else
                {
                    proxy->callMethod("StopRemoveNode").onInterface(IFACE_NAME).withArguments(lastSession);
                    logLine("StopRemoveNode session " + std::to_string(static_cast<unsigned>(lastSession)) + " issued");
                }
            }
        }
        catch (const sdbus::Error& err)
        {
            logLine(std::string{"D-Bus call failed: "} + err.what());
        }
    }

    endwin();
    connection->leaveEventLoop();
    return 0;
}
// NOLINTEND(readability-function-cognitive-complexity)
