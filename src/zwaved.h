//
// Created by martin on 4/26/26.
//

#ifndef ZWAVED_ZWAVED_H
#define ZWAVED_ZWAVED_H
// Constructor priorities run lowest-first; destructors run in reverse.
// The chain bakes in the daemon's startup invariants:
//
//   101 Logger        — comes up first; everyone after can log.
//   102 Config        — parses the config file and publishes the
//                       four retained config events on MessageBus,
//                       so Logger's threshold is applied *before*
//                       SignalHandler or any worker logs anything.
//   103 SignalHandler — registers SIGHUP/SIGTERM/SIGINT handlers
//                       and logs registration via Logger.
//   201..203          — dongle / protocol / external-api workers.
//
// Toolchain note: priorities 0..100 are reserved, so Logger gets the
// lowest available slot (101).
constexpr int CONFIG_LOGGER_PRIO             = 101;
constexpr int CONFIG_CONFIG_PRIO             = 102;
constexpr int CONFIG_ZWAVE_STARTUP_PRIO      = 103;
constexpr int CONFIG_ZWAVE_DONGLE_PRIO       = 201;
constexpr int CONFIG_ZWAVE_PROTOCOL_PRIO     = 202;
constexpr int CONFIG_ZWAVE_EXTERNAL_API_PRIO = 203;
#endif  // ZWAVED_ZWAVED_H
