// EncapProbe — E1 Tier 2 spike aid (#143). **Spike-only, off by default.**
// Compiled into the daemon ONLY when `-DZWAVED_SPIKE_ENCAP_LOG=ON`; a normal
// build excludes it entirely. It subscribes to the raw inbound
// `ApplicationCommand` bus event and logs any frame that decodes as a Multi
// Channel encapsulation (CC 0x60 CMD_ENCAP), so the spike operator can see —
// directly in the daemon log — whether a real device addresses one of the
// controller's endpoints. Read-only observability; it never sends anything.
//
// Remove (this file + the CMake option) once #143 reaches a go/no-go.
//
// It's a direct source of the `zwaved` executable (gated in
// src/spike/CMakeLists.txt), never archived, so its __attribute__((constructor))
// self-registration runs — same rule as the orchestrators (#140).

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../zwave-protocol/application/MultiChannel.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <iomanip>
#include <ios>
#include <sstream>
#include <string>

namespace
{
// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public member reads like a struct
struct State
{
    MessageBus::SubscriptionGuard sub;  // auto-unsubscribes on teardown

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
    const auto encap = MultiChannel::decodeEncap(event.ccData);
    if (!encap.has_value())
    {
        return;  // not a Multi Channel encapsulated frame — ignore
    }
    std::ostringstream stream;
    stream << "[encap-probe] node=" << static_cast<unsigned>(event.sourceNodeId)
           << " src-ep=" << static_cast<unsigned>(encap->sourceEndpoint)
           << " dst-ep=" << static_cast<unsigned>(encap->destinationEndpoint)
           << (encap->bitAddress ? " (bit-address)" : "") << " inner-len=" << encap->innerCommand.size()
           << " inner-cc=0x" << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(encap->innerCommand.empty() ? 0 : encap->innerCommand.front());
    Logger::info(stream.str());
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startEncapProbe() -> void
{
    state().sub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ApplicationCommand>(onApplicationCommand));
    Logger::info("[encap-probe] E1 Tier 2 spike build — logging inbound Multi Channel encap frames (#143)");
}
}  // namespace
