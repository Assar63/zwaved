// Inbound encapsulation-unwrap layer — the hand-written companion to the
// generated CcTranslator. Some inbound frames arrive wrapped in a
// transport/integrity CC; this module verifies/unwraps them and republishes
// the inner CC frame as a fresh ApplicationCommand, so the normal generated
// decoders (and the D-Bus raw-frame re-emit) handle it transparently —
// "every downstream decoder works unchanged."
//
// Today: CRC-16 Encapsulation (CC 0x56, #28) and Transport Service (CC 0x55,
// #25). The inner of a republished frame is never itself a 0x56 wrapper nor a
// 0x55 segment, so the re-entrant publish (under the recursive bus mutex)
// terminates after one hop.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../zwave-protocol/application/Crc16Encap.hpp"
#include "../zwave-protocol/application/TransportService.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_CC_TRANSLATOR_PRIO

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace
{
// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public member reads like a struct
struct State
{
    MessageBus::SubscriptionGuard sub;  // auto-unsubscribes on teardown
    // One Transport Service reassembler per source node (MVP: single in-flight
    // datagram per node — see TransportService::Assembler).
    std::map<std::uint8_t, TransportService::Assembler> assemblers;

    State()                                    = default;
    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
    ~State()                                   = default;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> State&
{
    static State instance;
    return instance;
}

auto onApplicationCommand(const MessageBus::ApplicationCommand& event) -> void
{
    const auto& data = event.ccData;
    if (data.size() < 2)
    {
        return;
    }

    // CRC-16 Encapsulation (CC 0x56, CRC16_ENCAP 0x01).
    if (data[0] == Crc16Encap::COMMAND_CLASS && data[1] == Crc16Encap::CRC16_ENCAP)
    {
        const auto inner = Crc16Encap::verifyAndUnwrap(std::span<const std::uint8_t>(data));
        if (!inner.has_value())
        {
            Logger::warn("[encapsulation] CRC-16 mismatch from node " + std::to_string(event.sourceNodeId) +
                         " — dropping frame");
            return;
        }
        MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = event.sourceNodeId, .ccData = *inner});
        return;
    }

    // Transport Service (CC 0x55) — feed each segment into the source node's
    // reassembler; republish the inner CC frame once the datagram is whole.
    if (data[0] == TransportService::COMMAND_CLASS)
    {
        auto& assembler  = state().assemblers[event.sourceNodeId];
        const auto inner = assembler.feedSegment(std::span<const std::uint8_t>(data));
        if (inner.has_value())
        {
            MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = event.sourceNodeId, .ccData = *inner});
        }
    }
}

__attribute__((constructor(CONFIG_CC_TRANSLATOR_PRIO))) auto startEncapsulationUnwrap() -> void
{
    state().sub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(onApplicationCommand));
}
}  // namespace
