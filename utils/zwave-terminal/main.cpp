#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <ncurses.h>
#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
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
    std::lock_guard<std::mutex> const lock(activity().mutex);
    activity().log.push_back(entry);
    while (activity().log.size() > MAX_LOG_LINES)
    {
        activity().log.pop_front();
    }
}

auto setDongleStatus(bool connected, const std::string& path) -> void
{
    std::lock_guard<std::mutex> const lock(activity().mutex);
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

auto draw(std::uint8_t lastSession) -> void
{
    erase();
    int row = 0;

    {
        std::lock_guard<std::mutex> const lock(activity().mutex);
        const std::string status =
            activity().dongleConnected ? "connected (" + activity().donglePath + ")" : "disconnected";
        mvprintw(row++, 0, " zwave-terminal  -  Dongle: %s", status.c_str());
    }
    mvhline(row++, 0, '-', getmaxx(stdscr));

    mvprintw(row++, 0, "  [1] Add zwave node");
    mvprintw(row++, 0, "  [2] Remove zwave node");
    mvprintw(row++, 0, "  [s] Stop current operation (session %u)", static_cast<unsigned>(lastSession));
    mvprintw(row++, 0, "  [q] Quit");
    mvhline(row++, 0, '-', getmaxx(stdscr));

    {
        std::lock_guard<std::mutex> const lock(activity().mutex);
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
               const std::vector<std::uint8_t>& /*ccs*/)
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
               const std::vector<std::uint8_t>& /*ccs*/)
            { logLine(formatStatusEntry("Exclusion", sessionId, status, nodeId)); });

    proxy.uponSignal("DongleStatus")
        .onInterface(IFACE_NAME)
        .call(
            [](bool connected, const std::string& path)
            {
                setDongleStatus(connected, path);
                logLine(connected ? "DongleStatus: connected " + path : "DongleStatus: disconnected");
            });
}
}  // namespace

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
