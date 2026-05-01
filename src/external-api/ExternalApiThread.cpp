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

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct ExternalApiState
{
    std::thread thread;
    std::atomic<bool> running{false};
    std::unique_ptr<ExternalApi::IBackend> backend;

    // The C++ runtime tears static-storage objects down via __cxa_atexit
    // *before* it runs __attribute__((destructor)) functions, so the
    // join must happen here — otherwise ~thread() fires while the
    // worker is still joinable and calls std::terminate. Construction
    // order (driven by __attribute__((constructor)) priority) means the
    // four modules' static state destructors fire in the same priority
    // order an explicit destructor function would have given us.
    ~ExternalApiState()
    {
        running = false;
        if (backend)
        {
            backend->stop();
        }
        if (thread.joinable())
        {
            thread.join();
        }
        std::cout << "External API thread shutdown complete\n";
    }

    ExternalApiState()                                               = default;
    ExternalApiState(const ExternalApiState&)                        = delete;
    auto operator=(const ExternalApiState&) -> ExternalApiState&     = delete;
    ExternalApiState(ExternalApiState&&) noexcept                    = delete;
    auto operator=(ExternalApiState&&) noexcept -> ExternalApiState& = delete;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

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

// Note: shutdown work lives in ExternalApiState's destructor (above) —
// running it from __attribute__((destructor)) here is too late, the
// C++ runtime has already destroyed the static state by the time the
// fini-array entries run.
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
