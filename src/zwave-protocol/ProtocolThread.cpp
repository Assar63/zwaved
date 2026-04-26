#include <thread>
#include <iostream>
#include <chrono>
#include <atomic>
#include <sys/prctl.h>
#include "../zwaved.h"

namespace
{
    std::thread thread;
    std::atomic<bool> running(false);

    void zwaveCommunicationThread()
    {
        prctl(PR_SET_NAME, "ZWaveComm", 0, 0, 0);
        while (running)
        {
            std::cout << "Z-Wave communication thread running\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << "Z-Wave communication thread stopping\n";
    }

    __attribute__((constructor(CONFIG_ZWAVE_PROTOCOL_PRIO))) void startZWaveThread()
    {
        running = true;
        thread = std::thread(zwaveCommunicationThread);
    }

    __attribute__((destructor(CONFIG_ZWAVE_PROTOCOL_PRIO))) void stopZWaveThread()
    {
        running = false;
        if (thread.joinable())
        {
            thread.join();
        }
    }
} // namespace
