// Constructor-armed bus wiring for the Security S0 network key (#163), the
// thin companion to the pure load-or-generate core in NetworkKey.cpp. At
// startup it resolves the key path from the retained SecurityConfig +
// StorageConfig events (both published by Config at priority 102), loads or
// generates the key, and publishes the retained S0NetworkKeyReady event so a
// future SecurityOrchestrator (#26) can defer secure work until the key
// exists. A load failure is logged and surfaced on the structured DaemonError
// feed — S0 is then unavailable, but the daemon still serves non-secure nodes.

#include "../../../logger/Logger.hpp"
#include "../../../message-bus/MessageBus.hpp"
#include "../../../zwaved.h"  // IWYU pragma: keep — CONFIG_SECURITY_PRIO
#include "NetworkKey.hpp"

#include <optional>
#include <string>

namespace
{
// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionGuard storageSub;
    MessageBus::SubscriptionGuard securitySub;
    std::string stateDir;
    std::string s0KeyFile;

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

auto loadKey() -> void
{
    const auto path   = S0::NetworkKey::resolvePath(state().s0KeyFile, state().stateDir);
    const auto loaded = S0::NetworkKey::loadOrGenerate(path);
    if (!loaded.has_value())
    {
        Logger::error("[s0] could not load or generate the network key at " + path.string() +
                      " — S0 secure operations unavailable");
        MessageBus::publish(MessageBus::DaemonError{
            .severity = MessageBus::DaemonError::SEVERITY_ERROR,
            .source   = "zwave-protocol",
            .code     = MessageBus::DaemonError::CODE_S0_KEY_FAILED,
            .message  = "S0 network key unavailable at " + path.string(),
        });
        return;
    }

    if (loaded->generated)
    {
        // Audit trail: key generation is a security-significant, one-time event.
        Logger::warn("[s0] generated a new network key at " + path.string() +
                     " — back it up; losing it forces re-inclusion of every secure node");
    }
    else
    {
        Logger::info("[s0] loaded the network key from " + path.string());
    }
    MessageBus::publish(MessageBus::S0NetworkKeyReady{.ready = true, .generated = loaded->generated});
}

__attribute__((constructor(CONFIG_SECURITY_PRIO))) auto startNetworkKeyService() -> void
{
    // Both config events are retained and already published by priority 102, so
    // each subscribe replays synchronously and the cached values are in place
    // before loadKey() resolves the path.
    state().storageSub  = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::StorageConfig>(
        [](const MessageBus::StorageConfig& cfg) -> void { state().stateDir = cfg.stateDir; }));
    state().securitySub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SecurityConfig>(
        [](const MessageBus::SecurityConfig& cfg) -> void { state().s0KeyFile = cfg.s0KeyFile; }));
    loadKey();
}
}  // namespace
