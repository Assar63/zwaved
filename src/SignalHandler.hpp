#ifndef SIGNAL_HANDLER_HPP
#define SIGNAL_HANDLER_HPP

#include <atomic>

// Global signal handling
// Note: SIGKILL cannot be caught or blocked - it terminates immediately
// We handle SIGHUP (reload/hangup) and SIGTERM (termination) instead

extern std::atomic<bool> applicationRunning;

#endif // SIGNAL_HANDLER_HPP

