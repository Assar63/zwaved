#include "SignalHandler.hpp"
#include "logger/Logger.hpp"

#include <chrono>
#include <thread>

auto main() -> int
{
    Logger::info("zwaved starting");
    while (isApplicationRunning())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    Logger::info("zwaved main loop exiting; subsystem destructors run next");
    return 0;
}
