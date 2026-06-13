// Constructor-armed bus wiring for the Security S2 network keys (#180) — the
// thin companion to the pure load-or-generate core in NetworkKeys.cpp. At
// startup it resolves the key directory from the retained SecurityConfig +
// StorageConfig events, loads or generates the four per-class keys, and
// publishes the retained S2NetworkKeysReady event so the later S2 phases can
// defer secure work until the keys exist. A load failure is logged + surfaced
// on the DaemonError feed; S2 is then unavailable but the daemon still runs.

#include "../../../logger/Logger.hpp"
#include "../../../message-bus/MessageBus.hpp"
#include "../../../zwaved.h"  // IWYU pragma: keep — CONFIG_SECURITY_PRIO
#include "NetworkKeys.hpp"

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
    std::string s2KeyDir;

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

auto loadKeys() -> void
{
    const auto dir    = S2::NetworkKeys::resolveDir(state().s2KeyDir, state().stateDir);
    const auto loaded = S2::NetworkKeys::loadOrGenerateAll(dir);
    if (!loaded.has_value())
    {
        Logger::error("[s2] could not load or generate the network keys in " + dir.string() +
                      " — S2 secure operations unavailable");
        MessageBus::publish(MessageBus::DaemonError{
            .severity = MessageBus::DaemonError::SEVERITY_ERROR,
            .source   = "zwave-protocol",
            .code     = MessageBus::DaemonError::CODE_S2_KEYS_FAILED,
            .message  = "S2 network keys unavailable in " + dir.string(),
        });
        return;
    }

    S2::NetworkKeys::setCurrent(loaded->keys);
    bool anyGenerated = false;
    for (const bool gen : loaded->generated)
    {
        anyGenerated = anyGenerated || gen;
    }
    if (anyGenerated)
    {
        // Audit trail: key generation is security-significant.
        Logger::warn("[s2] generated one or more network keys in " + dir.string() +
                     " — back them up; losing them forces re-inclusion (with DSK) of every secure node");
    }
    else
    {
        Logger::info("[s2] loaded the network keys from " + dir.string());
    }
    MessageBus::publish(MessageBus::S2NetworkKeysReady{.ready = true, .generated = anyGenerated});
}

__attribute__((constructor(CONFIG_SECURITY_PRIO))) auto startNetworkKeysService() -> void
{
    // Both config events are retained and already published by priority 102, so
    // each subscribe replays synchronously and the cached values are in place
    // before loadKeys() resolves the directory.
    state().storageSub  = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::StorageConfig>(
        [](const MessageBus::StorageConfig& cfg) -> void { state().stateDir = cfg.stateDir; }));
    state().securitySub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SecurityConfig>(
        [](const MessageBus::SecurityConfig& cfg) -> void { state().s2KeyDir = cfg.s2KeyDir; }));
    loadKeys();
}
}  // namespace
