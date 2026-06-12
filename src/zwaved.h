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
//   110 CcTranslator  — subscribes to bus ApplicationCommand events,
//                       runs application/ codec decoders, republishes
//                       typed bus events (e.g. BinarySwitchReport).
//                       Lets the external-api thread emit typed
//                       D-Bus signals without ever including a CC
//                       codec header — the bus is the only seam.
//   201..203          — dongle / protocol / external-api workers.
//   204 Orchestrators  — bus-only policy/state machines (WakeUp #68,
//                        Inclusion #67). Come up after the workers so
//                        every command subscriber is already wired;
//                        they own no thread and react synchronously to
//                        bus events.
//
// Toolchain note: priorities 0..100 are reserved, so Logger gets the
// lowest available slot (101).
constexpr int CONFIG_LOGGER_PRIO             = 101;
constexpr int CONFIG_CONFIG_PRIO             = 102;
constexpr int CONFIG_ZWAVE_STARTUP_PRIO      = 103;
constexpr int CONFIG_CC_TRANSLATOR_PRIO      = 110;
constexpr int CONFIG_SECURITY_PRIO           = 111;
constexpr int CONFIG_ZWAVE_DONGLE_PRIO       = 201;
constexpr int CONFIG_ZWAVE_PROTOCOL_PRIO     = 202;
constexpr int CONFIG_ZWAVE_EXTERNAL_API_PRIO = 203;
constexpr int CONFIG_ORCHESTRATOR_PRIO       = 204;
#endif  // ZWAVED_ZWAVED_H
