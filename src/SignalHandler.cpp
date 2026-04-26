#include "SignalHandler.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include "zwaved.h"

std::atomic<bool> applicationRunning(false);

namespace
{
    // Signal handler for SIGHUP (hangup/reload signal)
    void handleSignalHUP(const int signum)
    {
        std::cout << "\n[SIGHUP] Received hangup signal (" << signum << ")" << '\n';
        std::cout << "[SIGHUP] Reloading configuration...\n";
        // Configuration reload logic would go here
    }

    // Signal handler for SIGTERM (termination signal)
    void handleSignalTERM(const int signum)
    {
        std::cout << "\n[SIGTERM] Received termination signal (" << signum << ")\n";
        std::cout << "[SIGTERM] Gracefully shutting down...\n";
        applicationRunning = false;
    }

    // Signal handler for SIGINT (Ctrl+C)
    void handleSignalINT(const int signum)
    {
        std::cout << "\n[SIGINT] Received interrupt signal (" << signum << ")\n";
        std::cout << "[SIGINT] Gracefully shutting down...\n";
        applicationRunning = false;
    }

    __attribute__((constructor(CONFIG_ZWAVE_STARTUP_PRIO))) void constructor()
    {
        // Register SIGHUP handler
        struct sigaction actionHup = {};
        actionHup.sa_handler = handleSignalHUP;
        sigemptyset(&actionHup.sa_mask);
        actionHup.sa_flags = 0;
        sigaction(SIGHUP, &actionHup, nullptr);
        std::cout << "[SignalHandler] Registered SIGHUP handler\n";

        // Register SIGTERM handler
        struct sigaction actionTerm = {};
        actionTerm.sa_handler = handleSignalTERM;
        sigemptyset(&actionTerm.sa_mask);
        actionTerm.sa_flags = 0;
        sigaction(SIGTERM, &actionTerm, nullptr);
        std::cout << "[SignalHandler] Registered SIGTERM handler\n";

        // Register SIGINT handler (Ctrl+C)
        struct sigaction actionInt = {};
        actionInt.sa_handler = handleSignalINT;
        sigemptyset(&actionInt.sa_mask);
        actionInt.sa_flags = 0;
        sigaction(SIGINT, &actionInt, nullptr);
        std::cout << "[SignalHandler] Registered SIGINT handler\n";

        applicationRunning = true;
    }
} // namespace
