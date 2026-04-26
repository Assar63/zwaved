#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority
#include <atomic>
#include <chrono>
#include <iostream>
#include <sys/prctl.h>
#include <thread>

namespace
{
struct ZwaveProtocolState
{
    std::thread thread;
    std::atomic<bool> running{false};
};

auto state() -> ZwaveProtocolState&
{
    static ZwaveProtocolState instance;
    return instance;
}

auto zwaveCommunicationThread() -> void
{
    prctl(PR_SET_NAME, "ZWaveComm", 0, 0, 0);  // NOLINT(misc-include-cleaner): PR_SET_NAME from <sys/prctl.h>
    while (state().running)
    {
        std::cout << "Z-Wave communication thread running\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "Z-Wave communication thread stopping\n";
}

__attribute__((constructor(CONFIG_ZWAVE_PROTOCOL_PRIO))) auto startZWaveThread() -> void
{
    state().running = true;
    state().thread  = std::thread(zwaveCommunicationThread);
}

__attribute__((destructor(CONFIG_ZWAVE_PROTOCOL_PRIO))) auto stopZWaveThread() -> void
{
    state().running = false;
    if (state().thread.joinable())
    {
        state().thread.join();
    }
}
}  // namespace
