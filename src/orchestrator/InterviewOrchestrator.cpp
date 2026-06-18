// InterviewOrchestrator (#203) — auto-drives the post-inclusion node interview
// so device identity is gathered without a manual poke. Bus-only, priority 204.
//
// On a node joining, it runs an ordered sequence of Gets and advances on each
// matching typed report:
//   ManufacturerSpecific -> Version -> [Multi Channel endpoints if CC 0x60]
//   -> [Z-Wave+ Info if CC 0x5E] -> NodeInterviewComplete.
// The ManufacturerSpecific report in particular feeds PolicyRegister's
// noteDeviceIdentity, so device-level policy defaults become matchable.
//
// Ordering (SecurityBootstrap -> Interview): a node whose NIF advertises S0/S2
// is interviewed only after it's reported secure (NodeSecurityStatus), so the
// Gets ride the encrypted channel; a non-secure node is interviewed straight
// off NodeIncluded.
//
// MVP limits (like the other orchestrators): no inactivity timeout — a node
// that never answers a Get leaves its session parked; sleeping-node interview
// via PendingQueue and the InclusionOrchestrator policy-step re-trigger on
// NodeInterviewComplete are later #203 layers. Hardware-verified in #189.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_ORCHESTRATOR_PRIO

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint8_t CC_MULTI_CHANNEL  = 0x60;
constexpr std::uint8_t CC_ZWAVEPLUS_INFO = 0x5E;
constexpr std::uint8_t CC_SECURITY_0     = 0x98;
constexpr std::uint8_t CC_SECURITY_2     = 0x9F;
constexpr std::uint8_t NO_CALLBACK       = 0x00;
constexpr std::uint8_t SCHEME_NONE       = 0x00;  // NodeRegistry::SecurityScheme::None

enum class Step : std::uint8_t
{
    ManufacturerSpecific,
    Version,
    Endpoints,
    ZWavePlus,
};

struct Session
{
    std::vector<Step> steps;
    std::size_t index = 0;
    bool started      = false;  // false while deferred awaiting security bootstrap
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionGuard includedSub;
    MessageBus::SubscriptionGuard securitySub;
    MessageBus::SubscriptionGuard mfrSub;
    MessageBus::SubscriptionGuard versionSub;
    MessageBus::SubscriptionGuard endpointsSub;
    MessageBus::SubscriptionGuard zwavePlusSub;
    std::map<std::uint8_t, Session> sessions;

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

auto sendStep(std::uint8_t nodeId, Step step) -> void
{
    switch (step)
    {
    case Step::ManufacturerSpecific:
        MessageBus::publish(MessageBus::GetManufacturerSpecificCommand{.nodeId = nodeId, .callbackId = NO_CALLBACK});
        break;
    case Step::Version:
        MessageBus::publish(MessageBus::GetNodeVersionCommand{.nodeId = nodeId, .callbackId = NO_CALLBACK});
        break;
    case Step::Endpoints:
        MessageBus::publish(MessageBus::GetMultiChannelEndpointsCommand{.nodeId = nodeId, .callbackId = NO_CALLBACK});
        break;
    case Step::ZWavePlus:
        MessageBus::publish(MessageBus::GetZWavePlusInfoCommand{.nodeId = nodeId, .callbackId = NO_CALLBACK});
        break;
    }
}

auto beginInterview(std::uint8_t nodeId, Session& session) -> void
{
    session.started = true;
    session.index   = 0;
    Logger::info("[interview] node " + std::to_string(nodeId) + " — starting interview");
    sendStep(nodeId, session.steps.at(0));
}

// Advance a session if the report we just saw matches the step it's awaiting.
auto advance(std::uint8_t nodeId, Step completed) -> void
{
    const auto iter = state().sessions.find(nodeId);
    if (iter == state().sessions.end())
    {
        return;
    }
    Session& session = iter->second;
    if (!session.started || session.index >= session.steps.size() || session.steps.at(session.index) != completed)
    {
        return;  // not the step we're waiting on (stray / duplicate report)
    }
    ++session.index;
    if (session.index >= session.steps.size())
    {
        Logger::info("[interview] node " + std::to_string(nodeId) + " — interview complete");
        MessageBus::publish(MessageBus::NodeInterviewComplete{.nodeId = nodeId});
        state().sessions.erase(iter);
        return;
    }
    sendStep(nodeId, session.steps.at(session.index));
}

auto onNodeIncluded(const MessageBus::NodeIncluded& event) -> void
{
    const auto& ccs = event.commandClasses;
    const auto has  = [&ccs](std::uint8_t cls) { return std::find(ccs.begin(), ccs.end(), cls) != ccs.end(); };

    Session session;
    session.steps = {Step::ManufacturerSpecific, Step::Version};
    if (has(CC_MULTI_CHANNEL))
    {
        session.steps.push_back(Step::Endpoints);
    }
    if (has(CC_ZWAVEPLUS_INFO))
    {
        session.steps.push_back(Step::ZWavePlus);
    }

    const bool secure              = has(CC_SECURITY_0) || has(CC_SECURITY_2);
    state().sessions[event.nodeId] = std::move(session);
    if (secure)
    {
        // Defer until the node reports secure, so the Gets ride the encrypted
        // channel (NodeSecurityStatus arrives once bootstrap completes).
        Logger::info("[interview] node " + std::to_string(event.nodeId) +
                     " — deferring interview until secure bootstrap completes");
        return;
    }
    beginInterview(event.nodeId, state().sessions.at(event.nodeId));
}

auto onNodeSecurityStatus(const MessageBus::NodeSecurityStatus& event) -> void
{
    if (event.scheme == SCHEME_NONE)
    {
        return;
    }
    const auto iter = state().sessions.find(event.nodeId);
    if (iter != state().sessions.end() && !iter->second.started)
    {
        beginInterview(event.nodeId, iter->second);
    }
}

__attribute__((constructor(CONFIG_ORCHESTRATOR_PRIO))) auto startInterviewOrchestrator() -> void
{
    state().includedSub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeIncluded>(onNodeIncluded));
    state().securitySub =
        MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeSecurityStatus>(onNodeSecurityStatus));
    state().mfrSub       = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ManufacturerSpecificReport>(
        [](const MessageBus::ManufacturerSpecificReport& report) -> void
        { advance(report.sourceNodeId, Step::ManufacturerSpecific); }));
    state().versionSub   = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeVersionReport>(
        [](const MessageBus::NodeVersionReport& report) -> void { advance(report.sourceNodeId, Step::Version); }));
    state().endpointsSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::MultiChannelEndPointReport>(
        [](const MessageBus::MultiChannelEndPointReport& report) -> void
        { advance(report.sourceNodeId, Step::Endpoints); }));
    state().zwavePlusSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ZWavePlusInfoReport>(
        [](const MessageBus::ZWavePlusInfoReport& report) -> void { advance(report.sourceNodeId, Step::ZWavePlus); }));
}
}  // namespace
