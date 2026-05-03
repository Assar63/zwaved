#include "../message-bus/MessageBus.hpp"
#include "../zwave-protocol/application/BinarySwitch.hpp"
#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority

#include <cstdint>
#include <iostream>

// Cross-module bridge between the protocol layer and the external-api
// layer. The protocol thread publishes raw ApplicationCommand events
// (an opaque (rxStatus, sourceNodeId, ccData) tuple); subscribers in
// `src/external-api/` would otherwise need to include the CC codec
// headers from `src/zwave-protocol/application/` to decode those bytes
// into typed shapes. This module owns the codec calls instead and
// republishes the decoded reports on MessageBus as separate typed
// events (BinarySwitchReport, …). Effect: external-api keeps the bus
// as its only seam, and the codegen-emitted DBusSignals.gen.cpp can
// drive typed D-Bus signals straight off typed bus events with no
// CC-codec includes anywhere downstream of the translator.
//
// The translator runs in whatever thread published ApplicationCommand
// (today: the protocol thread, since MessageBus dispatch is
// synchronous on the publisher's thread). It has no thread of its
// own — just a static-init subscribe at priority CONFIG_CC_TRANSLATOR_PRIO
// and a static-state destructor that unsubscribes. New CCs land as
// extra `if (auto report = …; report.has_value())` blocks inside
// dispatch().

namespace
{
// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionId applicationCommandSub{0};

    ~State()
    {
        if (applicationCommandSub != 0)
        {
            MessageBus::unsubscribe(applicationCommandSub);
            applicationCommandSub = 0;
        }
        std::cout << "CC translator shutdown complete\n";
    }

    State()                                    = default;
    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> State&
{
    static State instance;
    return instance;
}

auto dispatch(const MessageBus::ApplicationCommand& event) -> void
{
    if (auto report = BinarySwitch::decodeReport(event.ccData); report.has_value())
    {
        MessageBus::publish(MessageBus::BinarySwitchReport{
            .sourceNodeId = event.sourceNodeId,
            .state        = static_cast<std::uint8_t>(report->state),
        });
    }
    // Future CCs slot in here as additional decode-and-publish blocks.
}

__attribute__((constructor(CONFIG_CC_TRANSLATOR_PRIO))) auto subscribe() -> void
{
    state().applicationCommandSub = MessageBus::subscribe<MessageBus::ApplicationCommand>(dispatch);
}
// Unsubscribe lives in State's destructor — running it via an
// __attribute__((destructor)) function would fire after __cxa_atexit
// has torn the static down, the same trap the other modules avoid.
}  // namespace
