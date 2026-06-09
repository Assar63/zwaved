// SceneOrchestrator (#121, #124) — runs daemon-side scenes from physical
// presses on real controllers. The cc-translator republishes inbound frames
// as typed bus events; this orchestrator resolves each against the scene
// store's trigger table (#120) and, on a hit, replays the bound scene's
// actions as SendDataCommands against the target nodes — then publishes
// SceneActivated for observers.
//
// Three trigger sources are supported (#124), distinguished by the store's
// `source` discriminator so the same (node, selector) can't collide:
//   - Central Scene (0x5B) — CentralSceneNotification, keyed on
//     (sourceNodeId, sceneNumber, keyAttribute)
//   - Basic Set (0x20)     — BasicSetReceived, keyed on (sourceNodeId, value)
//   - Scene Activation (0x2B) — SceneActivationSet, keyed on
//     (sourceNodeId, sceneId)
//
// Loose-coupling rule (same as the other orchestrators): bus-only, owns no
// thread, reacts synchronously to the bus event under the recursive bus
// mutex; the SendDataCommand / SceneActivated events it publishes are
// delivered depth-first before control returns to the cc-translator. Keying
// on the source node is what lets the same selector from different senders
// run different scenes (FUTURE.md E2).

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../scene-store/SceneStore.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
// Scene actions are fire-and-forget; the stored payload carries no callback
// correlation. 0 is the Serial API's "no callback requested" sentinel.
constexpr std::uint8_t NO_CALLBACK = 0x00;

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public member reads like a struct
struct State
{
    MessageBus::SubscriptionGuard sceneSub;            // Central Scene; auto-unsubscribes on teardown
    MessageBus::SubscriptionGuard basicSub;            // Basic Set
    MessageBus::SubscriptionGuard sceneActivationSub;  // Scene Activation

    State() = default;
    ~State()
    {
        Logger::info("[SceneOrchestrator] shutdown complete");
    }

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

// Resolve one press against the trigger table and, on a hit, replay the
// bound scene's actions then publish SceneActivated. `sceneNumber` /
// `keyAttribute` are the per-source selector fields (see SceneStore SOURCE_*).
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): wire fields, named at the call sites
auto runScene(std::uint8_t source,
              std::uint8_t sourceNodeId,
              std::uint8_t sceneNumber,
              std::uint8_t keyAttribute) -> void
{
    const std::optional<std::string> sceneId =
        SceneStore::instance().resolveTrigger(source, sourceNodeId, sceneNumber, keyAttribute);
    if (!sceneId.has_value())
    {
        return;  // press not bound to any scene — nothing to do
    }

    const std::optional<std::vector<SceneStore::Action>> actions = SceneStore::instance().getScene(*sceneId);
    std::uint32_t dispatched                                     = 0;
    if (actions.has_value())
    {
        for (const auto& action : *actions)
        {
            MessageBus::publish(MessageBus::SendDataCommand{
                .nodeId     = action.targetNodeId,
                .payload    = action.ccPayload,
                .callbackId = NO_CALLBACK,
            });
            ++dispatched;
        }
    }
    else
    {
        // A trigger pointing at a deleted scene — log it; the press is
        // still reported as activated with zero actions.
        Logger::warn("[SceneOrchestrator] trigger for node " + std::to_string(sourceNodeId) +
                     " resolved to missing scene '" + *sceneId + "'");
    }

    MessageBus::publish(MessageBus::SceneActivated{
        .sourceNodeId = sourceNodeId,
        .sceneNumber  = sceneNumber,
        .keyAttribute = keyAttribute,
        .sceneId      = *sceneId,
        .actionCount  = dispatched,
    });

    Logger::info("[SceneOrchestrator] source " + std::to_string(source) + " node " + std::to_string(sourceNodeId) +
                 " selector " + std::to_string(sceneNumber) + " → '" + *sceneId + "' (" + std::to_string(dispatched) +
                 " action(s))");
}

auto onCentralScene(const MessageBus::CentralSceneNotification& note) -> void
{
    runScene(SceneStore::SOURCE_CENTRAL_SCENE, note.sourceNodeId, note.sceneNumber, note.keyAttribute);
}

auto onBasicSet(const MessageBus::BasicSetReceived& note) -> void
{
    // Basic Set has no key attribute; the value is the selector.
    runScene(SceneStore::SOURCE_BASIC_SET, note.sourceNodeId, note.value, 0);
}

auto onSceneActivation(const MessageBus::SceneActivationSet& note) -> void
{
    // Scene Activation has no key attribute; the scene id is the selector.
    runScene(SceneStore::SOURCE_SCENE_ACTIVATION, note.sourceNodeId, note.sceneId, 0);
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startSceneOrchestrator() -> void
{
    state().sceneSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::CentralSceneNotification>(onCentralScene));
    state().basicSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::BasicSetReceived>(onBasicSet));
    state().sceneActivationSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::SceneActivationSet>(onSceneActivation));
    Logger::info("[SceneOrchestrator] watching for scene presses");
}
}  // namespace
