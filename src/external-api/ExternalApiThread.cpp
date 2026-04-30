#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority
#include "IExternalApi.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include <sys/prctl.h>

#ifdef ZWAVED_HAS_DBUS
#    include "DBusBackend.hpp"
#endif

namespace
{
constexpr int IDLE_SLEEP_MS = 200;

struct ExternalApiState
{
    std::thread thread;
    std::atomic<bool> running{false};
    std::unique_ptr<ExternalApi::IBackend> backend;
};

auto state() -> ExternalApiState&
{
    static ExternalApiState instance;
    return instance;
}

auto externalApiThread() -> void
{
    prctl(PR_SET_NAME, "ZWaveExtApi", 0, 0, 0);  // NOLINT(misc-include-cleaner): PR_SET_NAME from <sys/prctl.h>

    state().backend = ExternalApi::createBackend();
    if (!state().backend)
    {
        std::cerr << "[ExternalApi] no backend compiled; thread idling\n";
        while (state().running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(IDLE_SLEEP_MS));
        }
        return;
    }

    state().backend->run(state().running);
    std::cout << "[ExternalApi] backend run() returned\n";
}

__attribute__((constructor(CONFIG_ZWAVE_EXTERNAL_API_PRIO))) auto startExternalApiThread() -> void
{
    state().running = true;
    state().thread  = std::thread(externalApiThread);
}

__attribute__((destructor(CONFIG_ZWAVE_EXTERNAL_API_PRIO))) auto stopExternalApiThread() -> void
{
    state().running = false;
    if (state().backend)
    {
        state().backend->stop();
    }
    if (state().thread.joinable())
    {
        state().thread.join();
    }
    std::cout << "External API thread shutdown complete\n";
}
}  // namespace

namespace ExternalApi
{
auto createBackend() -> std::unique_ptr<IBackend>
{
#ifdef ZWAVED_HAS_DBUS
    return std::make_unique<DBusBackend>();
#else
    return nullptr;
#endif
}
}  // namespace ExternalApi
