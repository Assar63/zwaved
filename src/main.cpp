#include "SignalHandler.hpp"
#include "Version.hpp"
#include "logger/Logger.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include <unistd.h>  // _exit

namespace
{
auto versionString() -> std::string
{
    return std::string("zwaved ") + Version::SEMVER + " (" + Version::GIT_DESCRIBE + ")";
}

auto printHelp() -> void
{
    std::cout << "Usage: zwaved [OPTIONS]\n"
              << "\n"
              << "Z-Wave communication daemon. Owns a Z-Wave Serial API dongle and\n"
              << "exposes its host API to clients over D-Bus (system bus name\n"
              << "com.tiunda.ZWaved, object /com/tiunda/ZWaved).\n"
              << "\n"
              << "Options:\n"
              << "  -h, --help        Show this help and exit.\n"
              << "  -V, --version     Show the daemon version and exit.\n"
              << "\n"
              << "Environment:\n"
              << "  ZWAVED_CONFIG     Path to the daemon's config file. Default:\n"
              << "                    /etc/zwaved/zwaved.conf. A missing file is OK —\n"
              << "                    built-in defaults apply.\n"
              << "  ZWAVED_STATE_DIR  Override the directory holding nodes.db. Default:\n"
              << "                    /var/lib/zwaved (also overridable via the\n"
              << "                    config file's [storage] state_dir key).\n"
              << "\n"
              << "Files:\n"
              << "  /etc/zwaved/zwaved.conf  Runtime configuration (logger threshold,\n"
              << "                           accepted dongles, behaviour toggles).\n"
              << "                           Sample at etc/zwaved.conf in the repo.\n"
              << "  /etc/dbus-1/system.d/com.tiunda.ZWaved.conf\n"
              << "                           System-bus policy. Required for non-root\n"
              << "                           D-Bus clients.\n"
              << "\n"
              << "See MANUAL.md for the D-Bus method/signal reference and operator\n"
              << "walkthroughs (inclusion, exclusion, association, binary switch, ...).\n";
}
}  // namespace

auto main(int argc, char** argv) -> int
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg{argv[i]};  // NOLINT(*-pointer-arithmetic): argv is null-terminated
        if (arg == "--version" || arg == "-V")
        {
            std::cout << versionString() << '\n';
            std::cout.flush();
            // The constructor-priority threads (Logger, Config,
            // SignalHandler, dongle monitor, protocol, external API)
            // have already started before main() ran. _exit(0) skips
            // the destructor chain so a CLI flag doesn't emit a
            // stream of "X thread shutdown complete" lines — there's
            // no real work in flight to clean up at this point, and
            // the kernel reaps the worker threads on process exit.
            ::_exit(EXIT_SUCCESS);
        }
        if (arg == "--help" || arg == "-h")
        {
            printHelp();
            std::cout.flush();
            ::_exit(EXIT_SUCCESS);
        }
    }

    Logger::info(versionString() + " starting");
    while (isApplicationRunning())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    Logger::info("zwaved main loop exiting; subsystem destructors run next");
    return 0;
}
