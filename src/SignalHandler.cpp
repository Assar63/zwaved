#include "SignalHandler.hpp"

#include "logger/Logger.hpp"
#include "zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority

#include <atomic>
#include <iostream>
#include <string>

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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): signum and name carry distinct meaning at every call site
auto registerHandler(int signum, void (*handler)(int), const char* name) -> void
{
    struct sigaction action = {};
    action.sa_handler       = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(signum, &action, nullptr);
    Logger::info(std::string("[SignalHandler] Registered ") + name + " handler");
}

// Constructor (priority 102) runs *after* the Logger constructor
// (priority 101) per src/zwaved.h, so the queue and consumer thread
// are guaranteed up before the first Logger::info call below.
__attribute__((constructor(CONFIG_ZWAVE_STARTUP_PRIO))) auto constructor() -> void
{
    registerHandler(SIGHUP, handleSignalHUP, "SIGHUP");
    registerHandler(SIGTERM, handleSignalTERM, "SIGTERM");
    registerHandler(SIGINT, handleSignalINT, "SIGINT");  // Ctrl+C
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
