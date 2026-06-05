// SceneOrchestrator (#121) — runs daemon-side scenes from physical button
// presses. A Central Scene controller (wall switch / remote) associated to
// the controller node sends a CENTRAL_SCENE NOTIFICATION; the cc-translator
// republishes it as the typed CentralSceneNotification bus event. This
// orchestrator resolves the press `(sourceNodeId, sceneNumber, keyAttribute)`
// against the scene store's trigger table (#120) and, on a hit, replays the
// bound scene's actions as SendDataCommands against the target nodes — then
// publishes SceneActivated for observers.
//
// Loose-coupling rule (same as the other orchestrators): bus-only, owns no
// thread, reacts synchronously to the bus event under the recursive bus
// mutex; the SendDataCommand / SceneActivated events it publishes are
// delivered depth-first before control returns to the cc-translator. The
// `(sourceNodeId, sceneNumber, keyAttribute)` key is what lets the same scene
// number from different senders run different scenes (FUTURE.md E2).

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
    MessageBus::SubscriptionGuard sceneSub;  // auto-unsubscribes on teardown

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

auto onCentralScene(const MessageBus::CentralSceneNotification& note) -> void
{
    const std::optional<std::string> sceneId =
        SceneStore::instance().resolveTrigger(note.sourceNodeId, note.sceneNumber, note.keyAttribute);
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
        Logger::warn("[SceneOrchestrator] trigger for node " + std::to_string(note.sourceNodeId) +
                     " resolved to missing scene '" + *sceneId + "'");
    }

    MessageBus::publish(MessageBus::SceneActivated{
        .sourceNodeId = note.sourceNodeId,
        .sceneNumber  = note.sceneNumber,
        .keyAttribute = note.keyAttribute,
        .sceneId      = *sceneId,
        .actionCount  = dispatched,
    });

    Logger::info("[SceneOrchestrator] node " + std::to_string(note.sourceNodeId) + " scene " +
                 std::to_string(note.sceneNumber) + " → '" + *sceneId + "' (" + std::to_string(dispatched) +
                 " action(s))");
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startSceneOrchestrator() -> void
{
    state().sceneSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::CentralSceneNotification>(onCentralScene));
    Logger::info("[SceneOrchestrator] watching for scene presses");
}
}  // namespace
