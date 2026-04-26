#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include "SignalHandler.hpp"

int main()
{
    const auto* const lang = "C++";
    std::cout << "Hello and welcome to " << lang << "!\n";

    for (int i = 1; i <= 5; i++)
    {
        std::cout << "i = " << i << '\n';
    }

    while (applicationRunning)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Main application shutdown complete" << '\n';

    return 0;
}
