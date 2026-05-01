#ifndef ZWAVED_CONFIG_HPP
#define ZWAVED_CONFIG_HPP

/**
 * Daemon configuration loader.
 *
 * Reads `${ZWAVED_CONFIG:-/etc/zwaved/zwaved.conf}` (a minimal
 * INI-flavoured key/value file — no third-party parser involved) and
 * publishes the resolved values as **retained** events on
 * `MessageBus`:
 *
 *   - `LoggerConfig`    → `[logger]`
 *   - `StorageConfig`   → `[storage]`
 *   - `DonglesConfig`   → `[dongles]` (one or more `accept = …` rows)
 *   - `BehaviorConfig`  → `[behavior]`
 *
 * Consumers do **not** include this header. Each module subscribes to
 * the event(s) it cares about from its own constructor; the bus's
 * replay-on-subscribe semantics deliver the cached value
 * synchronously even if Config has already published. Defaults are
 * baked into the events themselves, so a missing config file is not
 * an error — the daemon publishes the defaults and continues.
 *
 * No `Config::snapshot()` accessor: the bus is the contract.
 */
namespace Config
{
/// Read the file (if present) and publish the four config events.
/// Idempotent — safe to call more than once, but the daemon only
/// invokes it from the priority-102 constructor.
auto load() -> void;
}  // namespace Config

#endif  // ZWAVED_CONFIG_HPP
