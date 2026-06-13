// Inbound encapsulation-unwrap layer — the hand-written companion to the
// generated CcTranslator. Some inbound frames arrive wrapped in a
// transport/integrity CC; this module verifies/unwraps them and republishes
// the inner CC frame as a fresh ApplicationCommand, so the normal generated
// decoders (and the D-Bus raw-frame re-emit) handle it transparently —
// "every downstream decoder works unchanged."
//
// Today: CRC-16 Encapsulation (CC 0x56, #28), Transport Service (CC 0x55,
// #25), and Security S0 MESSAGE_ENCAPSULATION (CC 0x98 / 0x81, #166). The inner
// of a republished frame is never itself one of those wrappers, so the
// re-entrant publish (under the recursive bus mutex) terminates after one hop.

#include "../zwave-protocol/security/s0/Encapsulation.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../node-registry/NodeRegistry.hpp"  // SecurityScheme
#include "../zwave-protocol/application/Crc16Encap.hpp"
#include "../zwave-protocol/application/TransportService.hpp"
#include "../zwave-protocol/security/s0/NetworkKey.hpp"
#include "../zwave-protocol/security/s0/NonceTable.hpp"
#include "../zwave-protocol/security/s0/Security.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_CC_TRANSLATOR_PRIO

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public member reads like a struct
struct State
{
    MessageBus::SubscriptionGuard sub;        // ApplicationCommand
    MessageBus::SubscriptionGuard dongleSub;  // DongleInfo (for controllerNodeId)
    // One Transport Service reassembler per source node (MVP: single in-flight
    // datagram per node — see TransportService::Assembler).
    std::map<std::uint8_t, TransportService::Assembler> assemblers;
    // Our own node id, needed as the S0 MAC's receiver id — learned from the
    // retained DongleInfo event.
    std::uint8_t controllerNodeId = 0;

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
        return;
    }

    // Security S0 MESSAGE_ENCAPSULATION (CC 0x98, cmd 0x81) — authenticate +
    // decrypt with the nonce we previously issued to this node and the network
    // key, then republish the inner CC frame for the normal decoders.
    if (data[0] == Security::COMMAND_CLASS && data[1] == Security::SECURITY_MESSAGE_ENCAPSULATION)
    {
        constexpr std::size_t nonceIdFromEnd = 9;   // receiver-nonce id (1) + MAC (8)
        constexpr std::size_t minFrame       = 20;  // CC + cmd + IV(8) + >=1 cipher + nonceId + MAC(8)
        if (data.size() < minFrame)
        {
            return;
        }
        const auto key = S0::NetworkKey::current();
        if (!key.has_value())
        {
            Logger::warn("[encapsulation] S0 frame from node " + std::to_string(event.sourceNodeId) +
                         " but no network key — dropping");
            return;
        }
        const std::uint8_t nonceId = data[data.size() - nonceIdFromEnd];
        const auto ourNonce        = S0::issuedNonces().take(event.sourceNodeId, nonceId, S0::NonceTable::Clock::now());
        if (!ourNonce.has_value())
        {
            Logger::warn("[encapsulation] S0 frame from node " + std::to_string(event.sourceNodeId) +
                         " references an unknown/expired nonce — dropping");
            return;
        }
        const auto inner = S0::Encapsulation::decrypt(
            std::span<const std::uint8_t>(data), event.sourceNodeId, state().controllerNodeId, *ourNonce, *key);
        if (!inner.has_value())
        {
            Logger::warn("[encapsulation] S0 authentication failed for node " + std::to_string(event.sourceNodeId) +
                         " — dropping");
            return;
        }
        MessageBus::publish(MessageBus::NodeSecurityStatus{
            .nodeId = event.sourceNodeId, .scheme = static_cast<std::uint8_t>(NodeRegistry::SecurityScheme::S0)});
        MessageBus::publish(MessageBus::ApplicationCommand{.sourceNodeId = event.sourceNodeId, .ccData = *inner});
    }
}

__attribute__((constructor(CONFIG_CC_TRANSLATOR_PRIO))) auto startEncapsulationUnwrap() -> void
{
    state().sub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(onApplicationCommand));
    state().dongleSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleInfo>(
        [](const MessageBus::DongleInfo& info) -> void { state().controllerNodeId = info.controllerNodeId; }));
}
}  // namespace
