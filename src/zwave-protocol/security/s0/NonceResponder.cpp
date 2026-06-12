// Inbound SECURITY_NONCE_GET responder (#164) — a bus-only reactor (no thread,
// constructor-armed at CONFIG_SECURITY_PRIO). When a node asks us for a nonce
// before sending an S0-encrypted frame, we generate one (recorded in the
// shared issuedNonces() table so the inbound decryptor in #165 can recover it),
// and ship a SECURITY_NONCE_REPORT back via SendDataCommand. The reply is
// plaintext and needs no network key, so it stands alone ahead of the
// encryption codec. The outbound "NONCE_GET -> wait -> encrypt -> send" flow
// is deferred to phase 5 integration, where the encryptor + send path meet.

#include "../../../logger/Logger.hpp"
#include "../../../message-bus/MessageBus.hpp"
#include "../../../zwaved.h"  // IWYU pragma: keep — CONFIG_SECURITY_PRIO
#include "NonceTable.hpp"
#include "Security.hpp"

#include <cstdint>
#include <string>

namespace
{
constexpr std::uint8_t NO_CALLBACK = 0x00;

auto onApplicationCommand(const MessageBus::ApplicationCommand& event) -> void
{
    const auto command = Security::commandByte(event.ccData);
    if (!command.has_value() || *command != Security::SECURITY_NONCE_GET)
    {
        return;
    }
    const auto nonce = S0::issuedNonces().generate(event.sourceNodeId, S0::NonceTable::Clock::now());
    Logger::debug("[s0] NONCE_GET from node " + std::to_string(event.sourceNodeId) + " — issuing nonce");
    MessageBus::publish(MessageBus::SendDataCommand{
        .nodeId     = event.sourceNodeId,
        .payload    = Security::encodeNonceReport(nonce),
        .callbackId = NO_CALLBACK,
    });
}

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public member reads like a struct
struct State
{
    MessageBus::SubscriptionGuard sub;

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

__attribute__((constructor(CONFIG_SECURITY_PRIO))) auto startNonceResponder() -> void
{
    state().sub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(onApplicationCommand));
}
}  // namespace
