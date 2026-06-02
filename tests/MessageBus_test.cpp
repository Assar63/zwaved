#include "MessageBus.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// Regression tests for the bus's reentrancy contract: a handler may
// publish() or subscribe() from inside itself and the nested call runs
// as a depth-first dispatch on the same thread. Before bus().mutex was
// made recursive this deadlocked — which silently broke every
// cc-translator typed-report republish and blocked WakeUpOrchestrator
// (#68). Each test scopes its subscriptions with SubscriptionGuard so
// they're torn down before the next test runs against the shared,
// process-wide bus singleton.

namespace
{
using MessageBus::SubscriptionGuard;
}  // namespace

// A handler that publishes another event must not deadlock; the nested
// event is fully delivered before the outer handler resumes.
TEST(MessageBusReentrancy, PublishFromHandlerDispatchesDepthFirst)
{
    std::vector<std::string> order;

    const SubscriptionGuard innerSub(MessageBus::subscribe<MessageBus::WakeUpNotification>(
        [&order](const MessageBus::WakeUpNotification& note)
        { order.push_back("wakeup:" + std::to_string(note.sourceNodeId)); }));

    const SubscriptionGuard outerSub(MessageBus::subscribe<MessageBus::ApplicationCommand>(
        [&order](const MessageBus::ApplicationCommand& cmd)
        {
            order.emplace_back("app:enter");
            // Reentrant publish — would self-deadlock on a plain mutex.
            MessageBus::publish(MessageBus::WakeUpNotification{.sourceNodeId = cmd.sourceNodeId});
            order.emplace_back("app:exit");
        }));

    MessageBus::publish(MessageBus::ApplicationCommand{.rxStatus = 0, .sourceNodeId = 7, .ccData = {0x84, 0x07}});

    // Depth-first: the nested wakeup is delivered between the outer
    // handler's enter and exit, not after it returns.
    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], "app:enter");
    EXPECT_EQ(order[1], "wakeup:7");
    EXPECT_EQ(order[2], "app:exit");
}

// A retained event replays its cached value to a late subscriber, once,
// on subscribe.
TEST(MessageBusReentrancy, RetainedReplayOnSubscribe)
{
    MessageBus::publish(MessageBus::DongleStatus{.connected = true, .ttyPath = "/dev/ttyACM9"});

    bool received = false;
    std::string seenPath;
    const SubscriptionGuard sub(MessageBus::subscribe<MessageBus::DongleStatus>(
        [&](const MessageBus::DongleStatus& status)
        {
            received = true;
            seenPath = status.ttyPath;
        }));

    EXPECT_TRUE(received);
    EXPECT_EQ(seenPath, "/dev/ttyACM9");
}

// subscribe() from inside a handler must not deadlock either — and the
// retained replay that fires during that reentrant subscribe (itself a
// nested dispatch under the recursive lock) must still be delivered.
TEST(MessageBusReentrancy, SubscribeFromHandlerWithReplay)
{
    MessageBus::publish(MessageBus::DongleStatus{.connected = true, .ttyPath = "/dev/ttyACM5"});

    bool innerReplayReceived = false;
    std::string innerSeenPath;
    SubscriptionGuard innerGuard;  // kept alive past the handler

    const SubscriptionGuard outerSub(MessageBus::subscribe<MessageBus::ApplicationCommand>(
        [&](const MessageBus::ApplicationCommand&)
        {
            // Reentrant subscribe to a retained type triggers an
            // immediate replay from inside this handler.
            innerGuard = SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleStatus>(
                [&](const MessageBus::DongleStatus& status)
                {
                    innerReplayReceived = true;
                    innerSeenPath       = status.ttyPath;
                }));
        }));

    MessageBus::publish(MessageBus::ApplicationCommand{.rxStatus = 0, .sourceNodeId = 1, .ccData = {}});

    EXPECT_TRUE(innerReplayReceived);
    EXPECT_EQ(innerSeenPath, "/dev/ttyACM5");
}
