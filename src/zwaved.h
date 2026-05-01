//
// Created by martin on 4/26/26.
//

#ifndef ZWAVED_ZWAVED_H
#define ZWAVED_ZWAVED_H
// Constructor priorities run lowest-first; destructors run in reverse.
// Logger comes up before everyone else and is torn down last, so:
//   * any constructor at a higher priority can call Logger::info(...);
//   * any destructor at a higher priority can do the same — the
//     consumer thread is still alive to drain the queue.
// Toolchain note: priorities 0..100 are reserved, so the Logger gets
// the lowest available slot (101) and SignalHandler is bumped to 102.
constexpr int CONFIG_LOGGER_PRIO             = 101;
constexpr int CONFIG_ZWAVE_STARTUP_PRIO      = 102;
constexpr int CONFIG_ZWAVE_DONGLE_PRIO       = 201;
constexpr int CONFIG_ZWAVE_PROTOCOL_PRIO     = 202;
constexpr int CONFIG_ZWAVE_EXTERNAL_API_PRIO = 203;
#endif  // ZWAVED_ZWAVED_H
