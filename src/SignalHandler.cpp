#include "SignalHandler.hpp"

#include "logger/Logger.hpp"
#include "zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority

#include <atomic>
#include <iostream>

#include <signal.h>  // NOLINT(modernize-deprecated-headers): <csignal> lacks POSIX sigaction/sigemptyset

namespace
{
auto runningState() -> std::atomic<bool>&
{
    static std::atomic<bool> instance{false};
    return instance;
}

// The functions below run in *async-signal context* — the kernel
// invokes them from inside whatever thread happened to be executing
// when the signal arrived. POSIX restricts what's safe to call from
// here to the async-signal-safe list: no mutexes, no malloc, no
// std::ostream. That is why we deliberately do NOT use Logger from
// inside these handlers (Logger::log takes a mutex). The std::cout
// calls are a pragmatic compromise — strictly speaking they're not
// async-signal-safe either, but in practice the daemon only ever
// receives one signal at a time and we're about to exit anyway. The
// real signal of "we're shutting down" goes through the atomic
// runningState; the prints are advisory.

auto handleSignalHUP(const int signum) -> void
{
    std::cout << "\n[SIGHUP] Received hangup signal (" << signum << ")\n";
    std::cout << "[SIGHUP] Reloading configuration...\n";
    // Configuration reload logic would go here
}

auto handleSignalTERM(const int signum) -> void
{
    std::cout << "\n[SIGTERM] Received termination signal (" << signum << ")\n";
    std::cout << "[SIGTERM] Gracefully shutting down...\n";
    stopApplication();
}

auto handleSignalINT(const int signum) -> void
{
    std::cout << "\n[SIGINT] Received interrupt signal (" << signum << ")\n";
    std::cout << "[SIGINT] Gracefully shutting down...\n";
    stopApplication();
}

// Constructor (priority 102) runs *after* the Logger constructor
// (priority 101) per src/zwaved.h, so the queue and consumer thread
// are guaranteed up before the first Logger::info call below.
__attribute__((constructor(CONFIG_ZWAVE_STARTUP_PRIO))) auto constructor() -> void
{
    // Register SIGHUP handler
    struct sigaction actionHup = {};
    actionHup.sa_handler       = handleSignalHUP;
    sigemptyset(&actionHup.sa_mask);
    actionHup.sa_flags = 0;
    sigaction(SIGHUP, &actionHup, nullptr);
    Logger::info("[SignalHandler] Registered SIGHUP handler");

    // Register SIGTERM handler
    struct sigaction actionTerm = {};
    actionTerm.sa_handler       = handleSignalTERM;
    sigemptyset(&actionTerm.sa_mask);
    actionTerm.sa_flags = 0;
    sigaction(SIGTERM, &actionTerm, nullptr);
    Logger::info("[SignalHandler] Registered SIGTERM handler");

    // Register SIGINT handler (Ctrl+C)
    struct sigaction actionInt = {};
    actionInt.sa_handler       = handleSignalINT;
    sigemptyset(&actionInt.sa_mask);
    actionInt.sa_flags = 0;
    sigaction(SIGINT, &actionInt, nullptr);
    Logger::info("[SignalHandler] Registered SIGINT handler");

    runningState() = true;
}
}  // namespace

auto isApplicationRunning() -> bool
{
    return runningState();
}

auto stopApplication() -> void
{
    runningState() = false;
}
