#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
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

// Full byte range for the Multilevel Switch level / duration prompts.
// The spec semantics ride on the sentinel values (0xFE = unknown,
// 0xFF = restore-last / default-duration), so we accept any byte and
// let the device interpret it.
constexpr int BYTE_MIN = 0x00;
constexpr int BYTE_MAX = 0xFF;

// Association group IDs are 1..255 per spec (0 reserved).
constexpr int GROUP_ID_MIN = 1;
constexpr int GROUP_ID_MAX = 255;

// Conventional Z-Wave lifeline association group.
constexpr std::uint8_t LIFELINE_GROUP = 1;

// FUNC_ID values used to decode `GetNetworkStatus`'s sessionCommandId
// field — kept identical to HostApi::CMD_*; we don't include HostApi
// here, the terminal is purely a D-Bus client.
constexpr std::uint8_t CMD_ADD_NODE    = 0x4A;
constexpr std::uint8_t CMD_REMOVE_NODE = 0x4B;

// Uptime formatting.
constexpr std::uint64_t SECONDS_PER_HOUR   = 3600;
constexpr std::uint64_t SECONDS_PER_MINUTE = 60;

// Max characters of node-id input read from the bottom-row prompt
// (3 digits + null terminator, with slack).
constexpr std::size_t NODE_ID_INPUT_BUFFER = 8;

// Wider input buffer for signed Configuration values (e.g. "-2147483648").
constexpr std::size_t INT_INPUT_BUFFER = 16;
// Free-form line input (e.g. a space/comma-separated association member list).
constexpr std::size_t LINE_INPUT_BUFFER = 64;

constexpr int DECIMAL_BASE      = 10;
constexpr int HEX_BASE          = 16;
constexpr std::uint32_t U16_MAX = 0xFFFFU;
constexpr std::uint32_t U32_MAX = 0xFFFFFFFFU;

// PolicyRegister BLOB wire format (see src/policy-register/PolicyRegister.cpp).
// Reimplemented here because the terminal is a standalone D-Bus client —
// it doesn't link daemon code, same as the NodeTuple / status-code
// duplication elsewhere in this file. The leading version byte makes a
// format change detectable rather than silently misparsed.
constexpr std::uint8_t POLICY_BLOB_VERSION    = 1;
constexpr std::uint8_t POLICY_KIND_CONFIG     = 1;
constexpr std::uint8_t POLICY_KIND_ASSOC      = 2;
constexpr std::uint8_t POLICY_KIND_WAKEUP     = 3;
constexpr std::size_t POLICY_CONFIG_BODY_LEN  = 7;  // parameter, size, signed, value(4)
constexpr std::size_t POLICY_ASSOC_HEADER_LEN = 2;  // groupId, memberCount
constexpr std::size_t POLICY_WAKEUP_BODY_LEN  = 5;  // intervalSeconds(4), notificationNodeId

// Valid Configuration value sizes per the CC spec.
constexpr int CONFIG_SIZE_MIN = 1;
constexpr int CONFIG_SIZE_MAX = 4;

constexpr unsigned BITS_PER_BYTE      = 8;
constexpr std::uint32_t U32_BYTE_MASK = 0xFFU;

// DaemonError severity values (mirror MessageBus::DaemonError::SEVERITY_*).
constexpr std::uint8_t SEVERITY_INFO     = 1;
constexpr std::uint8_t SEVERITY_WARN     = 2;
constexpr std::uint8_t SEVERITY_ERROR    = 3;
constexpr std::uint8_t SEVERITY_CRITICAL = 4;

// ncurses colour-pair ids for the DaemonError banner.
constexpr int CP_WARN     = 1;
constexpr int CP_ERROR    = 2;
constexpr int CP_CRITICAL = 3;

// Latest operator-visible daemon error (the retained DaemonError feed).
// An empty `message` means "no current problem" — banner hidden.
struct DaemonErrorState
{
    std::uint8_t severity = 0;
    std::string source;
    std::uint8_t code = 0;
    std::string message;
};

struct ActivityState
{
    std::mutex mutex;
    std::deque<std::string> log;
    bool dongleConnected{false};
    std::string donglePath;
    DaemonErrorState daemonError;
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire signature is fixed by the D-Bus signal/method
auto setDaemonError(std::uint8_t severity,
                    const std::string& source,
                    std::uint8_t code,
                    const std::string& message) -> void
{
    std::scoped_lock const lock(activity().mutex);
    activity().daemonError = DaemonErrorState{.severity = severity, .source = source, .code = code, .message = message};
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

/// Like promptByte but for a signed 32-bit integer (Configuration values
/// are i32). Same blocking-echo prompt mechanics. Returns std::nullopt on
/// empty input, parse error, or out-of-i32-range value.
auto promptInt32(const char* label) -> std::optional<std::int32_t>
{
    const int rows = getmaxy(stdscr);
    move(rows - 1, 0);
    clrtoeol();
    mvprintw(rows - 1, 0, "%s ", label);
    refresh();

    echo();
    curs_set(1);
    timeout(-1);  // blocking

    std::array<char, INT_INPUT_BUFFER> buffer{};
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
    std::int32_t value          = 0;
    auto const [ptr, errorCode] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (errorCode != std::errc{})
    {
        return std::nullopt;
    }
    return value;
}

/// Blocking bottom-row line prompt. Returns the entered text, or nullopt
/// on empty input / read error. Shared by the typed prompts below.
auto promptLine(const char* label) -> std::optional<std::string>
{
    const int rows = getmaxy(stdscr);
    move(rows - 1, 0);
    clrtoeol();
    mvprintw(rows - 1, 0, "%s ", label);
    refresh();

    echo();
    curs_set(1);
    timeout(-1);  // blocking

    std::array<char, LINE_INPUT_BUFFER> buffer{};
    int const got = getnstr(buffer.data(), static_cast<int>(buffer.size()) - 1);

    noecho();
    curs_set(0);
    timeout(UI_REFRESH_MS);

    if (got != OK)
    {
        return std::nullopt;
    }
    std::string text(buffer.data());
    if (text.empty())
    {
        return std::nullopt;
    }
    return text;
}

/// Parse an unsigned integer, accepting a `0x` prefix for hex. nullopt on
/// trailing junk or out-of-range.
auto parseUint(const std::string& text, std::uint32_t maxVal) -> std::optional<std::uint32_t>
{
    const char* begin = text.data();
    const char* end   = begin + text.size();
    int base          = DECIMAL_BASE;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        begin += 2;
        base = HEX_BASE;
    }
    std::uint32_t value         = 0;
    auto const [ptr, errorCode] = std::from_chars(begin, end, value, base);
    if (errorCode != std::errc{} || ptr != end || value > maxVal)
    {
        return std::nullopt;
    }
    return value;
}

auto promptU16(const char* label) -> std::optional<std::uint16_t>
{
    auto text = promptLine(label);
    if (!text.has_value())
    {
        return std::nullopt;
    }
    auto value = parseUint(*text, U16_MAX);
    if (!value.has_value())
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*value);
}

auto promptU32(const char* label) -> std::optional<std::uint32_t>
{
    auto text = promptLine(label);
    if (!text.has_value())
    {
        return std::nullopt;
    }
    return parseUint(*text, U32_MAX);
}

/// Read a single character from a line prompt, restricted to `valid`.
auto promptChar(const char* label, const std::string& valid) -> std::optional<char>
{
    auto text = promptLine(label);
    if (!text.has_value())
    {
        return std::nullopt;
    }
    const char chosen = (*text)[0];
    if (valid.find(chosen) == std::string::npos)
    {
        return std::nullopt;
    }
    return chosen;
}

/// Parse a space/comma-separated list of node IDs (each 1..232). Requires
/// at least one. nullopt on any malformed / out-of-range token.
auto promptNodeList(const char* label) -> std::optional<std::vector<std::uint8_t>>
{
    auto text = promptLine(label);
    if (!text.has_value())
    {
        return std::nullopt;
    }
    for (auto& character : *text)
    {
        if (character == ',')
        {
            character = ' ';
        }
    }
    std::istringstream stream(*text);
    std::vector<std::uint8_t> members;
    int member = 0;
    while (stream >> member)
    {
        if (member < NODE_ID_MIN || member > NODE_ID_MAX)
        {
            return std::nullopt;
        }
        members.push_back(static_cast<std::uint8_t>(member));
    }
    if (!stream.eof() || members.empty())
    {
        return std::nullopt;
    }
    return members;
}

// One entry in a modal submenu: a trigger key, a label, and the action to
// run when chosen.
struct MenuItem
{
    char key;
    std::string label;
    std::function<void()> action;
};

// Render a modal submenu overlay, block for a key, and run the matching
// action. Any unmatched key (including Esc) cancels. Keeps each submenu's
// key namespace independent of the top-level menu, so adding a CC is just
// appending an item — no more single-key exhaustion (#109).
auto runActionMenu(const char* title, const std::vector<MenuItem>& items) -> void
{
    erase();
    int row = 0;
    mvprintw(row++, 0, " %s  —  press a key (any other to cancel)", title);
    mvhline(row++, 0, '-', getmaxx(stdscr));
    for (const auto& item : items)
    {
        mvprintw(row++, 0, "  [%c] %s", item.key, item.label.c_str());
    }
    refresh();

    timeout(-1);  // blocking
    const int key = getch();
    timeout(UI_REFRESH_MS);

    for (const auto& item : items)
    {
        if (item.key == key)
        {
            item.action();
            return;
        }
    }
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

        // DaemonError banner — only shown while there's a current error
        // (empty message == recovered). Colour-coded by severity.
        const auto& err = activity().daemonError;
        if (!err.message.empty())
        {
            const char* label = "INFO";
            int colorPair     = 0;
            switch (err.severity)
            {
            case SEVERITY_WARN:
                label     = "WARN";
                colorPair = CP_WARN;
                break;
            case SEVERITY_ERROR:
                label     = "ERROR";
                colorPair = CP_ERROR;
                break;
            case SEVERITY_CRITICAL:
                label     = "CRITICAL";
                colorPair = CP_CRITICAL;
                break;
            default:
                break;
            }
            const bool coloured = colorPair != 0 && has_colors();
            if (coloured)
            {
                attron(COLOR_PAIR(colorPair) | A_BOLD);
            }
            mvprintw(row++,
                     0,
                     " ! %s [%s code=0x%02X]: %s",
                     label,
                     err.source.c_str(),
                     static_cast<unsigned>(err.code),
                     err.message.c_str());
            if (coloured)
            {
                attroff(COLOR_PAIR(colorPair) | A_BOLD);
            }
        }
    }
    mvhline(row++, 0, '-', getmaxx(stdscr));

    mvprintw(row++, 0, "  [1] Add zwave node          [2] Remove zwave node");
    mvprintw(row++, 0, "  [g] Get from node…          [c] Control / set on node…");
    mvprintw(row++, 0, "  [p] Policy…");
    mvprintw(row++, 0, "  [l] List included nodes     [n] Network status     [i] Dongle info");
    mvprintw(row++, 0, "  [f] Remove failed node      [L] Set lifeline (controller -> group 1)");
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

// Human name for a Sensor Multilevel sensor type (the common subset of
// the SDS13781 table); unknown types render as bare hex.
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
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

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

// ---- Policy (#66/#69) ------------------------------------------------
// A decoded policy entry. Tagged by `kind`; only the matching fields are
// meaningful. Mirrors PolicyRegister::PolicyEntry without pulling in the
// daemon's variant type.
struct PolicyEntry
{
    std::uint8_t kind = 0;
    // POLICY_KIND_CONFIG
    std::uint8_t parameter = 0;
    std::uint8_t size      = 1;
    bool isSigned          = false;
    std::int32_t value     = 0;
    // POLICY_KIND_ASSOC
    std::uint8_t groupId = 0;
    std::vector<std::uint8_t> members;
    // POLICY_KIND_WAKEUP
    std::uint32_t intervalSeconds   = 0;
    std::uint8_t notificationNodeId = 0;
};

// Read a big-endian u32 at `pos`, advancing it. Caller has bounds-checked.
auto readU32Be(const std::vector<std::uint8_t>& bytes, std::size_t& pos) -> std::uint32_t
{
    const std::uint32_t value = (static_cast<std::uint32_t>(bytes[pos]) << (3 * BITS_PER_BYTE)) |
                                (static_cast<std::uint32_t>(bytes[pos + 1]) << (2 * BITS_PER_BYTE)) |
                                (static_cast<std::uint32_t>(bytes[pos + 2]) << BITS_PER_BYTE) |
                                static_cast<std::uint32_t>(bytes[pos + 3]);
    pos += 4;
    return value;
}

auto appendU32Be(std::vector<std::uint8_t>& out, std::uint32_t value) -> void
{
    out.push_back(static_cast<std::uint8_t>((value >> (3 * BITS_PER_BYTE)) & U32_BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>((value >> (2 * BITS_PER_BYTE)) & U32_BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>((value >> BITS_PER_BYTE) & U32_BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>(value & U32_BYTE_MASK));
}

auto decodePolicy(const std::vector<std::uint8_t>& bytes) -> std::optional<std::vector<PolicyEntry>>
{
    std::size_t pos = 0;
    if (bytes.size() < 2 || bytes[pos++] != POLICY_BLOB_VERSION)
    {
        return std::nullopt;
    }
    const std::uint8_t count = bytes[pos++];
    std::vector<PolicyEntry> out;
    for (std::uint8_t idx = 0; idx < count; ++idx)
    {
        if (pos >= bytes.size())
        {
            return std::nullopt;
        }
        PolicyEntry entry;
        entry.kind = bytes[pos++];
        if (entry.kind == POLICY_KIND_CONFIG)
        {
            if (pos + POLICY_CONFIG_BODY_LEN > bytes.size())
            {
                return std::nullopt;
            }
            entry.parameter = bytes[pos++];
            entry.size      = bytes[pos++];
            entry.isSigned  = bytes[pos++] != 0;
            entry.value     = static_cast<std::int32_t>(readU32Be(bytes, pos));
        }
        else if (entry.kind == POLICY_KIND_ASSOC)
        {
            if (pos + POLICY_ASSOC_HEADER_LEN > bytes.size())
            {
                return std::nullopt;
            }
            entry.groupId               = bytes[pos++];
            const std::uint8_t memberCt = bytes[pos++];
            if (pos + memberCt > bytes.size())
            {
                return std::nullopt;
            }
            for (std::uint8_t member = 0; member < memberCt; ++member)
            {
                entry.members.push_back(bytes[pos++]);
            }
        }
        else if (entry.kind == POLICY_KIND_WAKEUP)
        {
            if (pos + POLICY_WAKEUP_BODY_LEN > bytes.size())
            {
                return std::nullopt;
            }
            entry.intervalSeconds    = readU32Be(bytes, pos);
            entry.notificationNodeId = bytes[pos++];
        }
        else
        {
            return std::nullopt;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

auto encodePolicy(const std::vector<PolicyEntry>& policy) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    out.push_back(POLICY_BLOB_VERSION);
    out.push_back(static_cast<std::uint8_t>(policy.size()));
    for (const auto& entry : policy)
    {
        out.push_back(entry.kind);
        if (entry.kind == POLICY_KIND_CONFIG)
        {
            out.push_back(entry.parameter);
            out.push_back(entry.size);
            out.push_back(entry.isSigned ? 1 : 0);
            appendU32Be(out, static_cast<std::uint32_t>(entry.value));
        }
        else if (entry.kind == POLICY_KIND_ASSOC)
        {
            out.push_back(entry.groupId);
            out.push_back(static_cast<std::uint8_t>(entry.members.size()));
            for (const auto member : entry.members)
            {
                out.push_back(member);
            }
        }
        else if (entry.kind == POLICY_KIND_WAKEUP)
        {
            appendU32Be(out, entry.intervalSeconds);
            out.push_back(entry.notificationNodeId);
        }
    }
    return out;
}

// Log a decoded policy under `header`. Empty policy logs "(empty)";
// a blob that fails to decode logs a hex dump so it's still visible.
auto logPolicy(const std::string& header, const std::vector<std::uint8_t>& bytes) -> void
{
    logLine(header);
    if (bytes.empty())
    {
        logLine("    (none)");
        return;
    }
    const auto policy = decodePolicy(bytes);
    if (!policy.has_value())
    {
        logLine("    (undecodable blob, " + std::to_string(bytes.size()) + " bytes)");
        return;
    }
    if (policy->empty())
    {
        logLine("    (empty)");
        return;
    }
    for (const auto& entry : *policy)
    {
        std::ostringstream stream;
        if (entry.kind == POLICY_KIND_CONFIG)
        {
            stream << "    config param=" << static_cast<unsigned>(entry.parameter)
                   << " size=" << static_cast<unsigned>(entry.size) << " value=" << entry.value
                   << (entry.isSigned ? " (signed)" : "");
        }
        else if (entry.kind == POLICY_KIND_ASSOC)
        {
            stream << "    assoc group=" << static_cast<unsigned>(entry.groupId) << " members=[";
            bool first = true;
            for (const auto member : entry.members)
            {
                if (!first)
                {
                    stream << " ";
                }
                first = false;
                stream << static_cast<unsigned>(member);
            }
            stream << "]";
        }
        else if (entry.kind == POLICY_KIND_WAKEUP)
        {
            stream << "    wakeup interval=" << entry.intervalSeconds
                   << "s notify=" << static_cast<unsigned>(entry.notificationNodeId);
        }
        logLine(stream.str());
    }
}

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
auto promptPolicyEntry() -> std::optional<PolicyEntry>
{
    auto kind = promptChar("Entry kind: [c]onfiguration  [a]ssociation  [w]ake-up:", "caw");
    if (!kind.has_value())
    {
        logLine("policy entry: cancelled");
        return std::nullopt;
    }
    if (*kind == 'c')
    {
        auto parameter = promptByte("Config parameter (0-255):", BYTE_MIN, BYTE_MAX);
        auto size      = promptByte("Value size bytes (1, 2, or 4):", CONFIG_SIZE_MIN, CONFIG_SIZE_MAX);
        auto value     = promptInt32("Value (signed int32):");
        if (!parameter.has_value() || !size.has_value() || !value.has_value() ||
            (*size != 1 && *size != 2 && *size != 4))
        {
            logLine("config entry: cancelled or invalid (size must be 1/2/4)");
            return std::nullopt;
        }
        return PolicyEntry{.kind      = POLICY_KIND_CONFIG,
                           .parameter = *parameter,
                           .size      = *size,
                           .isSigned  = *value < 0,
                           .value     = *value};
    }
    if (*kind == 'a')
    {
        auto group   = promptByte("Group id (1-255):", GROUP_ID_MIN, GROUP_ID_MAX);
        auto members = promptNodeList("Member node ids (space/comma separated):");
        if (!group.has_value() || !members.has_value())
        {
            logLine("association entry: cancelled or invalid");
            return std::nullopt;
        }
        return PolicyEntry{.kind = POLICY_KIND_ASSOC, .groupId = *group, .members = *members};
    }
    // wake-up
    auto interval = promptU32("Interval seconds (0..16777215):");
    auto notify   = promptByte("Notify node id (0=controller):", BYTE_MIN, BYTE_MAX);
    if (!interval.has_value() || !notify.has_value())
    {
        logLine("wake-up entry: cancelled or invalid");
        return std::nullopt;
    }
    return PolicyEntry{.kind = POLICY_KIND_WAKEUP, .intervalSeconds = *interval, .notificationNodeId = *notify};
}

// True iff two entries occupy the same policy slot — an override of this
// identity replaces rather than appends (Configuration keyed by
// parameter, Association by groupId, Wake-Up a singleton).
auto sameSlot(const PolicyEntry& lhs, const PolicyEntry& rhs) -> bool
{
    if (lhs.kind != rhs.kind)
    {
        return false;
    }
    if (lhs.kind == POLICY_KIND_CONFIG)
    {
        return lhs.parameter == rhs.parameter;
    }
    if (lhs.kind == POLICY_KIND_ASSOC)
    {
        return lhs.groupId == rhs.groupId;
    }
    return true;  // wake-up singleton
}

// Decode the existing blob, upsert `entry` (replace same-slot or append),
// and return the re-encoded blob. nullopt if the existing blob is
// non-empty but undecodable — don't clobber data we can't read. `replaced`
// reports whether an existing entry was overwritten.
auto applyEntryToBlob(const std::vector<std::uint8_t>& existing,
                      const PolicyEntry& entry,
                      bool& replaced) -> std::optional<std::vector<std::uint8_t>>
{
    std::vector<PolicyEntry> policy;
    if (!existing.empty())
    {
        auto decoded = decodePolicy(existing);
        if (!decoded.has_value())
        {
            return std::nullopt;
        }
        policy = *decoded;
    }
    replaced = false;
    for (auto& existingEntry : policy)
    {
        if (sameSlot(existingEntry, entry))
        {
            existingEntry = entry;
            replaced      = true;
            break;
        }
    }
    if (!replaced)
    {
        policy.push_back(entry);
    }
    return encodePolicy(policy);
}

// One-line summary of an entry for log messages.
auto entrySummary(const PolicyEntry& entry) -> std::string
{
    std::ostringstream stream;
    if (entry.kind == POLICY_KIND_CONFIG)
    {
        stream << "config param=" << static_cast<unsigned>(entry.parameter) << " value=" << entry.value;
    }
    else if (entry.kind == POLICY_KIND_ASSOC)
    {
        stream << "assoc group=" << static_cast<unsigned>(entry.groupId) << " members=" << entry.members.size();
    }
    else
    {
        stream << "wakeup interval=" << entry.intervalSeconds << "s";
    }
    return stream.str();
}

// Add or update a policy entry (Configuration / Association / Wake-Up) in
// a node's override, preserving any other entries: read the current
// override, decode it, upsert the entry, re-encode, and write it back.
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

    // Colour pairs for the DaemonError banner. -1 background keeps the
    // terminal's default; use_default_colors() makes -1 valid.
    if (has_colors())
    {
        start_color();
        use_default_colors();
        init_pair(CP_WARN, COLOR_YELLOW, -1);
        init_pair(CP_ERROR, COLOR_RED, -1);
        init_pair(CP_CRITICAL, COLOR_WHITE, COLOR_RED);
    }

    std::uint8_t sessionCounter = 0;
    std::uint8_t lastSession    = 0;
    bool lastWasAdd             = false;
    bool running                = true;

    logLine(std::string{"Connected to "} + BUS_NAME);

    // Pick up any error the daemon is already reporting (the DaemonError
    // feed is retained, but a D-Bus signal isn't replayed to late
    // subscribers — so query the cached value once at startup).
    try
    {
        using DaemonErrorTuple = sdbus::Struct<std::uint8_t, std::string, std::uint8_t, std::string>;
        DaemonErrorTuple current;
        proxy->callMethod("GetDaemonError").onInterface(IFACE_NAME).storeResultsTo(current);
        setDaemonError(std::get<0>(current), std::get<1>(current), std::get<2>(current), std::get<3>(current));
    }
    catch (const sdbus::Error& err)
    {
        logLine(std::string{"GetDaemonError failed: "} + err.what());
    }

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
            else if (key == 'g' || key == 'G')
            {
                runActionMenu(
                    "Get from node",
                    {
                        {'b', "Battery", [&] { handleSimpleGet(*proxy, sessionCounter, "GetBattery"); }},
                        {'v', "Version", [&] { handleSimpleGet(*proxy, sessionCounter, "GetNodeVersion"); }},
                        {'m',
                         "Manufacturer-specific",
                         [&] { handleSimpleGet(*proxy, sessionCounter, "GetManufacturerSpecific"); }},
                        {'z', "Z-Wave Plus info", [&] { handleSimpleGet(*proxy, sessionCounter, "GetZWavePlusInfo"); }},
                        {'c', "Configuration", [&] { handleGetConfiguration(*proxy, sessionCounter); }},
                        {'s',
                         "Sensor multilevel",
                         [&] { handleSimpleGet(*proxy, sessionCounter, "GetSensorMultilevel"); }},
                        {'i', "Binary sensor", [&] { handleSimpleGet(*proxy, sessionCounter, "GetSensorBinary"); }},
                        {'e', "Meter", [&] { handleGetMeter(*proxy, sessionCounter); }},
                        {'n', "Notification", [&] { handleGetNotification(*proxy, sessionCounter); }},
                        {'t', "Thermostat mode", [&] { handleSimpleGet(*proxy, sessionCounter, "GetThermostatMode"); }},
                        {'p', "Thermostat setpoint", [&] { handleGetThermostatSetpoint(*proxy, sessionCounter); }},
                        {'o',
                         "Thermostat operating state",
                         [&] { handleSimpleGet(*proxy, sessionCounter, "GetThermostatOperatingState"); }},
                        {'f',
                         "Thermostat fan mode",
                         [&] { handleSimpleGet(*proxy, sessionCounter, "GetThermostatFanMode"); }},
                        {'w', "Multilevel switch", [&] { handleGetMultilevelSwitch(*proxy, sessionCounter); }},
                        {'a', "Association members", [&] { handleGetAssociation(*proxy, sessionCounter); }},
                        {'r', "Association groupings", [&] { handleGetAssociationGroupings(*proxy, sessionCounter); }},
                    });
            }
            else if (key == 'c' || key == 'C')
            {
                runActionMenu(
                    "Control / set on node",
                    {
                        {'o', "Switch binary ON", [&] { handleSwitchBinary(*proxy, sessionCounter, true); }},
                        {'f', "Switch binary OFF", [&] { handleSwitchBinary(*proxy, sessionCounter, false); }},
                        {'w', "Multilevel switch set", [&] { handleSetMultilevelSwitch(*proxy, sessionCounter); }},
                        {'b', "Basic set", [&] { handleSetBasic(*proxy, sessionCounter); }},
                        {'c', "Configuration set", [&] { handleSetConfiguration(*proxy, sessionCounter); }},
                        {'t', "Thermostat mode set", [&] { handleSetThermostatMode(*proxy, sessionCounter); }},
                        {'p', "Thermostat setpoint set", [&] { handleSetThermostatSetpoint(*proxy, sessionCounter); }},
                        {'n', "Thermostat fan mode set", [&] { handleSetThermostatFanMode(*proxy, sessionCounter); }},
                        {'k', "Wake-up interval", [&] { handleSetWakeUpInterval(*proxy, sessionCounter); }},
                        {'a',
                         "Association add",
                         [&] { handleAssociationEdit(*proxy, sessionCounter, "SetAssociation"); }},
                        {'r',
                         "Association remove",
                         [&] { handleAssociationEdit(*proxy, sessionCounter, "RemoveAssociation"); }},
                    });
            }
            else if (key == 'p' || key == 'P')
            {
                runActionMenu("Policy",
                              {
                                  {'e', "View effective policy", [&] { handleViewEffectivePolicy(*proxy); }},
                                  {'o', "View node override", [&] { handleViewNodeOverride(*proxy); }},
                                  {'s', "Set node override entry", [&] { handleSetNodeOverrideEntry(*proxy); }},
                                  {'d', "Delete node override", [&] { handleDeleteNodeOverride(*proxy); }},
                                  {'l', "List device policies", [&] { handleListDevicePolicies(*proxy); }},
                                  {'a', "Device policy authoring", [&] { handleDevicePolicyEdit(*proxy); }},
                              });
            }
            else if (key == 'l')
            {
                handleListNodes(*proxy);
            }
            else if (key == 'n' || key == 'N')
            {
                handleNetworkStatus(*proxy);
            }
            else if (key == 'f' || key == 'F')
            {
                handleRemoveFailedNode(*proxy, sessionCounter);
            }
            else if (key == 'i' || key == 'I')
            {
                handleDongleInfo(*proxy);
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
