#ifndef SIGNAL_HANDLER_HPP
#define SIGNAL_HANDLER_HPP

// Note: SIGKILL cannot be caught or blocked - it terminates immediately
// We handle SIGHUP (reload/hangup) and SIGTERM (termination) instead

auto isApplicationRunning() -> bool;
auto stopApplication() -> void;

#endif  // SIGNAL_HANDLER_HPP
