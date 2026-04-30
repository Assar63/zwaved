#include "SignalHandler.hpp"

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

// Signal handler for SIGHUP (hangup/reload signal)
auto handleSignalHUP(const int signum) -> void
{
    std::cout << "\n[SIGHUP] Received hangup signal (" << signum << ")" << '\n';
    std::cout << "[SIGHUP] Reloading configuration...\n";
    // Configuration reload logic would go here
}

// Signal handler for SIGTERM (termination signal)
auto handleSignalTERM(const int signum) -> void
{
    std::cout << "\n[SIGTERM] Received termination signal (" << signum << ")\n";
    std::cout << "[SIGTERM] Gracefully shutting down...\n";
    stopApplication();
}

// Signal handler for SIGINT (Ctrl+C)
auto handleSignalINT(const int signum) -> void
{
    std::cout << "\n[SIGINT] Received interrupt signal (" << signum << ")\n";
    std::cout << "[SIGINT] Gracefully shutting down...\n";
    stopApplication();
}

__attribute__((constructor(CONFIG_ZWAVE_STARTUP_PRIO))) auto constructor() -> void
{
    // Register SIGHUP handler
    struct sigaction actionHup = {};
    actionHup.sa_handler       = handleSignalHUP;
    sigemptyset(&actionHup.sa_mask);
    actionHup.sa_flags = 0;
    sigaction(SIGHUP, &actionHup, nullptr);
    std::cout << "[SignalHandler] Registered SIGHUP handler\n";

    // Register SIGTERM handler
    struct sigaction actionTerm = {};
    actionTerm.sa_handler       = handleSignalTERM;
    sigemptyset(&actionTerm.sa_mask);
    actionTerm.sa_flags = 0;
    sigaction(SIGTERM, &actionTerm, nullptr);
    std::cout << "[SignalHandler] Registered SIGTERM handler\n";

    // Register SIGINT handler (Ctrl+C)
    struct sigaction actionInt = {};
    actionInt.sa_handler       = handleSignalINT;
    sigemptyset(&actionInt.sa_mask);
    actionInt.sa_flags = 0;
    sigaction(SIGINT, &actionInt, nullptr);
    std::cout << "[SignalHandler] Registered SIGINT handler\n";

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
