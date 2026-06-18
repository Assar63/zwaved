// Bus-driven test for the InterviewOrchestrator (#203): drive NodeIncluded +
// the typed reports and assert the Get sequence and the NodeInterviewComplete.

#include "MessageBus.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CC_MULTI_CHANNEL  = 0x60;
constexpr std::uint8_t CC_ZWAVEPLUS_INFO = 0x5E;
constexpr std::uint8_t CC_SECURITY_2     = 0x9F;

struct Capture
{
    std::vector<std::uint8_t> mfr;
    std::vector<std::uint8_t> version;
    std::vector<std::uint8_t> endpoints;
    std::vector<std::uint8_t> zwavePlus;
    std::optional<std::uint8_t> completed;
    MessageBus::SubscriptionGuard g1, g2, g3, g4, g5;

    Capture()
    {
        g1 = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::GetManufacturerSpecificCommand>(
            [this](const MessageBus::GetManufacturerSpecificCommand& cmd) { mfr.push_back(cmd.nodeId); }));
        g2 = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::GetNodeVersionCommand>(
            [this](const MessageBus::GetNodeVersionCommand& cmd) { version.push_back(cmd.nodeId); }));
        g3 = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::GetMultiChannelEndpointsCommand>(
            [this](const MessageBus::GetMultiChannelEndpointsCommand& cmd) { endpoints.push_back(cmd.nodeId); }));
        g4 = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::GetZWavePlusInfoCommand>(
            [this](const MessageBus::GetZWavePlusInfoCommand& cmd) { zwavePlus.push_back(cmd.nodeId); }));
        g5 = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::NodeInterviewComplete>(
            [this](const MessageBus::NodeInterviewComplete& evt) { completed = evt.nodeId; }));
    }
};
}  // namespace

TEST(InterviewOrchestrator, FullSequenceForRichNode)
{
    constexpr std::uint8_t node = 7;
    Capture cap;

    MessageBus::publish(
        MessageBus::NodeIncluded{.nodeId = node, .commandClasses = {0x25, CC_MULTI_CHANNEL, CC_ZWAVEPLUS_INFO}});
    ASSERT_EQ(cap.mfr.size(), 1U);  // ManufacturerSpecific first
    EXPECT_EQ(cap.mfr[0], node);

    MessageBus::publish(MessageBus::ManufacturerSpecificReport{.sourceNodeId = node});
    ASSERT_EQ(cap.version.size(), 1U);  // -> Version

    MessageBus::publish(MessageBus::NodeVersionReport{.sourceNodeId = node});
    ASSERT_EQ(cap.endpoints.size(), 1U);  // -> Multi Channel endpoints (CC 0x60 present)

    MessageBus::publish(MessageBus::MultiChannelEndPointReport{.sourceNodeId = node});
    ASSERT_EQ(cap.zwavePlus.size(), 1U);  // -> Z-Wave+ (CC 0x5E present)

    EXPECT_FALSE(cap.completed.has_value());
    MessageBus::publish(MessageBus::ZWavePlusInfoReport{.sourceNodeId = node});
    ASSERT_TRUE(cap.completed.has_value());
    EXPECT_EQ(*cap.completed, node);
}

TEST(InterviewOrchestrator, MinimalNodeSkipsOptionalSteps)
{
    constexpr std::uint8_t node = 8;
    Capture cap;

    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = node, .commandClasses = {0x25}});  // no 0x60 / 0x5E
    MessageBus::publish(MessageBus::ManufacturerSpecificReport{.sourceNodeId = node});
    MessageBus::publish(MessageBus::NodeVersionReport{.sourceNodeId = node});

    EXPECT_TRUE(cap.endpoints.empty());
    EXPECT_TRUE(cap.zwavePlus.empty());
    ASSERT_TRUE(cap.completed.has_value());
    EXPECT_EQ(*cap.completed, node);
}

TEST(InterviewOrchestrator, SecureNodeDefersUntilSecure)
{
    constexpr std::uint8_t node = 9;
    Capture cap;

    // NIF advertises S2 → interview is deferred (no Get yet).
    MessageBus::publish(MessageBus::NodeIncluded{.nodeId = node, .commandClasses = {0x25, CC_SECURITY_2}});
    EXPECT_TRUE(cap.mfr.empty());

    // Once the node reports secure, the interview starts (over the encrypted channel).
    MessageBus::publish(MessageBus::NodeSecurityStatus{.nodeId = node, .scheme = 2});  // S2Unauthenticated
    ASSERT_EQ(cap.mfr.size(), 1U);
    EXPECT_EQ(cap.mfr[0], node);
}
