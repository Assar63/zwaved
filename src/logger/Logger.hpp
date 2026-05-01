#ifndef ZWAVED_LOGGER_HPP
#define ZWAVED_LOGGER_HPP

#include <cstdint>
#include <string>

/**
 * Asynchronous, thread-safe logger.
 *
 * Producers append a log entry to an MPSC queue and return immediately
 * — formatting and I/O happen on a dedicated consumer thread named
 * `ZWaveLog` so a slow sink (syslog under load, a journal flush, a
 * blocked stdout pipe) never stalls the protocol or external-API
 * threads.
 *
 * The sink is selected at build time via the `ZWAVED_LOGGER_SINK`
 * CMake cache option:
 *
 *   - `stdout` (default) — writes formatted lines to stdout. Under
 *     systemd the journal captures them automatically and they end up
 *     under `/var/log/journal/`; queryable via `journalctl -u zwaved`.
 *   - `syslog` — calls `syslog(3)` with severity mapped from Level.
 *     Suitable for OpenWRT / non-systemd deployments where logd or
 *     rsyslog routes to `/var/log/messages`.
 *
 * Logger comes up at constructor priority 100 (before every other
 * subsystem) and tears down last, so any startup or shutdown trace
 * lands on the wire.
 */
namespace Logger
{
enum class Level : std::uint8_t
{
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
};

/// Enqueue a log entry. Producer-side: takes the queue mutex briefly
/// to push, never blocks on I/O. Safe to call from any thread,
/// including from inside MessageBus handlers.
auto log(Level level, std::string message) -> void;

/// Convenience wrappers — equivalent to `log(<Level>, ...)`.
auto debug(std::string message) -> void;
auto info(std::string message) -> void;
auto warn(std::string message) -> void;
auto error(std::string message) -> void;

/// Detach stdin from the controlling terminal and reroute stdout /
/// stderr through Logger so any rogue printf, std::cout, std::cerr,
/// library panic or assert trace ends up in the same pipeline as the
/// rest of the daemon's output. Idempotent.
///
/// **Build constraint:** capturing stdout only makes sense when the
/// configured sink isn't itself stdout — under the `stdout` sink the
/// Logger consumer thread writes there directly, so capturing would
/// loop the output back through its own pipe. Therefore:
///
///   - `ZWAVED_LOGGER_SINK = syslog` — engaged automatically from
///     Logger's constructor; this function is normally a no-op for
///     callers because the work is already done.
///   - `ZWAVED_LOGGER_SINK = stdout` — this function is a no-op even
///     when called explicitly, by design.
auto claimStandardStreams() -> void;
}  // namespace Logger

#endif  // ZWAVED_LOGGER_HPP
