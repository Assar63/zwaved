// InclusionOrchestrator (#67) — post-inclusion setup state machine.
//
// When a node finishes inclusion, several things should happen before it
// is "ready": its lifeline (Association group 1 → controller) must be
// populated (Z-Wave Plus nodes ship it empty and expect the including
// controller to fill it), and any per-device / per-node policy
// (Configuration parameters, extra Associations, Wake-Up interval) should
// be applied. This used to live inline in ProtocolThread's inclusion
// callback; promoting it to a dedicated orchestrator means adding a new
// post-inclusion step is "add a branch here," not "edit ProtocolThread,"
// and leaves a clean seam for a future SecurityOrchestrator (#26/#27) to
// gate on before the policy step.
//
// Ordering (#203): the lifeline runs on NodeIncluded (the node needs the
// controller in group 1 early), but the *policy* step waits for
// NodeInterviewComplete — the device identity PolicyRegister keys device-level
// defaults on is only learned during the interview, so the post-inclusion order
// is SecurityBootstrap → Interview → Policy. The supported-CC set is cached
// between the two events.
//
// Loose-coupling rule: bus only. It consumes the high-level NodeIncluded
// event (ProtocolThread already knows which raw status codes mean "done")
// and the effective policy from PolicyRegister (#66), and emits
// Set*Command events for ProtocolThread to encode + send. Each step is
// fire-and-forget — no waiting on SendDataCallback acknowledgement (that
// reliability layer belongs to Supervision CC #14). Failures don't roll
// back; they log / publish DaemonError and move on. Handlers run as
// nested dispatches under the recursive bus mutex (see MessageBus.hpp).

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../policy-register/PolicyRegister.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace
{
// Supported-CC gating constants. COMMAND_CLASS_MARK separates the
// supported half of a NIF (CCs the node answers) from the controlled
// half (CCs it emits); we only act on the supported half — there's no
// point SET-ing a CC the node merely emits.
constexpr std::uint8_t CC_CONFIGURATION  = 0x70;
constexpr std::uint8_t CC_WAKE_UP        = 0x84;
constexpr std::uint8_t CC_ASSOCIATION    = 0x85;
constexpr std::uint8_t CC_MARK           = 0xEF;
constexpr std::uint8_t LIFELINE_GROUP_ID = 1;
constexpr std::uint8_t NO_CALLBACK       = 0x00;

struct SupportedCcs
{
    bool association   = false;
    bool configuration = false;
    bool wakeUp        = false;
};

// Scan only the supported half (up to COMMAND_CLASS_MARK) for the CCs
// the orchestrator cares about.
auto scanSupported(const std::vector<std::uint8_t>& commandClasses) -> SupportedCcs
{
    SupportedCcs found;
    for (const auto cls : commandClasses)
    {
        if (cls == CC_MARK)
        {
            break;
        }
        if (cls == CC_ASSOCIATION)
        {
            found.association = true;
        }
        else if (cls == CC_CONFIGURATION)
        {
            found.configuration = true;
        }
        else if (cls == CC_WAKE_UP)
        {
            found.wakeUp = true;
        }
    }
    return found;
}

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, members read like a struct
struct State
{
    MessageBus::SubscriptionGuard includedSub;
    MessageBus::SubscriptionGuard interviewSub;
    MessageBus::SubscriptionGuard dongleInfoSub;
    MessageBus::SubscriptionGuard behaviorSub;

    // Cached from retained DongleInfo / BehaviorConfig. Only ever touched
    // from bus handlers, which the bus serializes under one mutex, so no
    // atomics are needed.
    std::optional<std::uint8_t> controllerNodeId;
    bool autoLifeline = true;

    // Supported-CC set captured at NodeIncluded, consumed at
    // NodeInterviewComplete (the policy step waits for device identity).
    std::map<std::uint8_t, SupportedCcs> pendingPolicy;

    State() = default;
    ~State()
    {
        Logger::info("[InclusionOrchestrator] shutdown complete");
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

// Apply one effective-policy entry, gated on the node actually supporting
// the matching CC. Returns true if a command was published.
auto applyPolicyEntry(std::uint8_t nodeId,
                      const SupportedCcs& supported,
                      const PolicyRegister::PolicyEntry& entry) -> bool
{
    return std::visit(
        [&](const auto& concrete) -> bool
        {
            using T = std::decay_t<decltype(concrete)>;
            if constexpr (std::is_same_v<T, PolicyRegister::ConfigurationEntry>)
            {
                if (!supported.configuration)
                {
                    return false;
                }
                MessageBus::publish(MessageBus::SetConfigurationCommand{
                    .nodeId     = nodeId,
                    .parameter  = concrete.parameter,
                    .size       = concrete.size,
                    .isSigned   = concrete.isSigned,
                    .value      = concrete.value,
                    .callbackId = NO_CALLBACK,
                });
                return true;
            }
            else if constexpr (std::is_same_v<T, PolicyRegister::AssociationEntry>)
            {
                if (!supported.association)
                {
                    return false;
                }
                MessageBus::publish(MessageBus::SetAssociationCommand{
                    .nodeId     = nodeId,
                    .groupId    = concrete.groupId,
                    .members    = concrete.members,
                    .callbackId = NO_CALLBACK,
                });
                return true;
            }
            else  // WakeUpEntry
            {
                if (!supported.wakeUp)
                {
                    return false;
                }
                // A policy may leave notificationNodeId unset (0) to mean
                // "report to the daemon's controller"; fall back to it.
                const std::uint8_t notify = concrete.notificationNodeId != 0 ? concrete.notificationNodeId
                                                                             : state().controllerNodeId.value_or(0);
                MessageBus::publish(MessageBus::SetWakeUpIntervalCommand{
                    .nodeId           = nodeId,
                    .seconds          = concrete.intervalSeconds,
                    .controllerNodeId = notify,
                    .callbackId       = NO_CALLBACK,
                });
                return true;
            }
        },
        entry);
}

auto onNodeIncluded(const MessageBus::NodeIncluded& event) -> void
{
    const std::uint8_t nodeId    = event.nodeId;
    const SupportedCcs supported = scanSupported(event.commandClasses);

    // Lifeline first: ensure the controller is in group 1 so the node can
    // reach us with its reports. Gated on the auto_lifeline toggle, the
    // node supporting Association, and us knowing our own controller id.
    if (supported.association && state().autoLifeline)
    {
        if (state().controllerNodeId.has_value())
        {
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
            const std::uint8_t controller = *state().controllerNodeId;
            MessageBus::publish(MessageBus::SetAssociationCommand{
                .nodeId     = nodeId,
                .groupId    = LIFELINE_GROUP_ID,
                .members    = {controller},
                .callbackId = NO_CALLBACK,
            });
            MessageBus::publish(MessageBus::InclusionLifelineSet{.nodeId = nodeId});
            Logger::info("[InclusionOrchestrator] lifeline set node=" + std::to_string(nodeId) +
                         " controller=" + std::to_string(controller));
        }
        else
        {
            Logger::warn("[InclusionOrchestrator] lifeline skipped for node " + std::to_string(nodeId) +
                         " — controller node id unknown");
            MessageBus::publish(MessageBus::DaemonError{
                .severity = MessageBus::DaemonError::SEVERITY_WARN,
                .source   = "orchestrator",
                .code     = MessageBus::DaemonError::CODE_ORCHESTRATOR_NO_CONTROLLER,
                .message  = "inclusion lifeline skipped — controller node id unknown",
            });
        }
    }

    // The policy step waits for NodeInterviewComplete (#203): the device
    // identity PolicyRegister keys device-level defaults on is learned during
    // the interview, so applying here (at NodeIncluded) would miss them. Cache
    // the supported CCs for the policy step to gate on.
    state().pendingPolicy[nodeId] = supported;
}

// Apply the effective policy once the interview has run (device identity known).
auto onNodeInterviewComplete(const MessageBus::NodeInterviewComplete& event) -> void
{
    const std::uint8_t nodeId = event.nodeId;
    SupportedCcs supported;
    const auto cached = state().pendingPolicy.find(nodeId);
    if (cached != state().pendingPolicy.end())
    {
        supported = cached->second;
        state().pendingPolicy.erase(cached);
    }

    // Effective policy = device default (now matchable — identity is known)
    // merged with the per-node override. Each entry gated on the supported CC.
    const PolicyRegister::Policy policy = PolicyRegister::instance().effectivePolicy(nodeId);
    std::uint32_t entriesApplied        = 0;
    for (const auto& entry : policy)
    {
        if (applyPolicyEntry(nodeId, supported, entry))
        {
            ++entriesApplied;
        }
    }

    MessageBus::publish(MessageBus::InclusionPolicyApplied{.nodeId = nodeId, .entriesApplied = entriesApplied});
    MessageBus::publish(MessageBus::InclusionComplete{.nodeId = nodeId});
    Logger::info("[InclusionOrchestrator] node " + std::to_string(nodeId) + " setup complete — " +
                 std::to_string(entriesApplied) + " policy entr(ies) applied");
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startInclusionOrchestrator() -> void
{
    state().dongleInfoSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleInfo>(
        [](const MessageBus::DongleInfo& info) -> void { state().controllerNodeId = info.controllerNodeId; }));
    state().behaviorSub   = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::BehaviorConfig>(
        [](const MessageBus::BehaviorConfig& cfg) -> void { state().autoLifeline = cfg.autoLifeline; }));
    state().includedSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeIncluded>(onNodeIncluded));
    state().interviewSub = MessageBus::SubscriptionGuard(
        MessageBus::subscribe<MessageBus::NodeInterviewComplete>(onNodeInterviewComplete));
    Logger::info("[InclusionOrchestrator] watching for inclusions");
}
}  // namespace
