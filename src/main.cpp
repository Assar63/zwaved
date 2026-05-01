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
            // the destructor chain so `zwaved --version` doesn't emit
            // a stream of "X thread shutdown complete" lines —
            // there's no real work in flight to clean up at this
            // point, and the kernel reaps the worker threads on
            // process exit.
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
