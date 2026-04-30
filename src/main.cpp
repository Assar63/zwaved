#include "SignalHandler.hpp"

#include <chrono>
#include <iostream>
#include <thread>

auto main() -> int
{
    const auto* const lang = "C++";
    std::cout << "Hello and welcome to " << lang << "!\n";
    while (isApplicationRunning())
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Main application shutdown complete, now shutting down threads" << '\n';

    return 0;
}
