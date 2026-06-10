#include "activity.hpp"
#include "constants.hpp"
#include "handlers.hpp"
#include "render.hpp"
#include "signal_handlers.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <ncurses.h>
#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IProxy.h>
#include <sdbus-c++/Types.h>
#include <sdbus-c++/sdbus-c++.h>

namespace zwt
{
namespace
{
// NOLINTBEGIN(readability-function-cognitive-complexity): flat key-dispatch table
auto run() -> int
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
                        {'k', "Color switch", [&] { handleGetColorSwitch(*proxy, sessionCounter); }},
                        {'d', "Door lock", [&] { handleSimpleGet(*proxy, sessionCounter, "GetDoorLock"); }},
                        {'u', "User code", [&] { handleGetUserCode(*proxy, sessionCounter); }},
                        {'x',
                         "User code slot count",
                         [&] { handleSimpleGet(*proxy, sessionCounter, "GetUserCodeCount"); }},
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
                        {'l', "Color switch set (RGB)", [&] { handleSetColorSwitch(*proxy, sessionCounter); }},
                        {'d', "Door lock set", [&] { handleSetDoorLock(*proxy, sessionCounter); }},
                        {'u', "User code set", [&] { handleSetUserCode(*proxy, sessionCounter); }},
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
            else if (key == 'e' || key == 'E')
            {
                runActionMenu("Scenes",
                              {
                                  {'l', "List scenes", [&] { handleListScenes(*proxy); }},
                                  {'t', "List triggers", [&] { handleListSceneTriggers(*proxy); }},
                                  {'s', "Set / edit scene", [&] { handleSetScene(*proxy); }},
                                  {'d', "Delete scene", [&] { handleDeleteScene(*proxy); }},
                                  {'b', "Bind trigger (press -> scene)", [&] { handleBindSceneTrigger(*proxy); }},
                                  {'u', "Unbind trigger", [&] { handleUnbindSceneTrigger(*proxy); }},
                              });
            }
            else if (key == 'h' || key == 'H')
            {
                runActionMenu("Logical thermostat (climate group)",
                              {
                                  {'m', "Set group mode", [&] { handleSetLogicalThermostatMode(*proxy); }},
                                  {'s', "Set group setpoint", [&] { handleSetLogicalThermostatSetpoint(*proxy); }},
                                  {'g', "Get group state", [&] { handleGetLogicalThermostatState(*proxy); }},
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
}  // namespace
}  // namespace zwt

auto main() -> int
{
    return zwt::run();
}
