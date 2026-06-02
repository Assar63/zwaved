#include "PolicyRegister.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace
{
using PolicyRegister::AssociationEntry;
using PolicyRegister::ConfigurationEntry;
using PolicyRegister::DeviceId;
using PolicyRegister::Policy;
using PolicyRegister::WakeUpEntry;

const std::vector<std::uint8_t> kHomeId{0xE2, 0xA1, 0xB0, 0x7C};

constexpr DeviceId kDevice{.manufacturerId = 0x0086, .productTypeId = 0x0002, .productId = 0x0064};

class PolicyRegisterTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const auto unique = std::to_string(::getpid()) + "-" + std::to_string(std::random_device{}());
        dbPath_           = std::filesystem::temp_directory_path() / ("zwaved-policy-test-" + unique + ".db");
    }
    void TearDown() override
    {
        std::error_code errorCode;
        std::filesystem::remove(dbPath_, errorCode);
    }
    std::filesystem::path dbPath_;
};
}  // namespace

// ---- Serialization --------------------------------------------------

TEST(PolicySerialization, RoundTripsAllEntryKinds)
{
    Policy policy;
    policy.emplace_back(ConfigurationEntry{.parameter = 3, .size = 2, .isSigned = true, .value = -1234});
    policy.emplace_back(AssociationEntry{.groupId = 1, .members = {1, 5, 7}});
    policy.emplace_back(WakeUpEntry{.intervalSeconds = 3600, .notificationNodeId = 1});

    const auto bytes     = PolicyRegister::serialize(policy);
    const auto roundtrip = PolicyRegister::deserialize(bytes);
    ASSERT_TRUE(roundtrip.has_value());
    ASSERT_EQ(roundtrip->size(), 3U);

    const auto& cfg = std::get<ConfigurationEntry>((*roundtrip)[0]);
    EXPECT_EQ(cfg.parameter, 3);
    EXPECT_EQ(cfg.size, 2);
    EXPECT_TRUE(cfg.isSigned);
    EXPECT_EQ(cfg.value, -1234);

    const auto& assoc = std::get<AssociationEntry>((*roundtrip)[1]);
    EXPECT_EQ(assoc.groupId, 1);
    EXPECT_EQ(assoc.members, (std::vector<std::uint8_t>{1, 5, 7}));

    const auto& wake = std::get<WakeUpEntry>((*roundtrip)[2]);
    EXPECT_EQ(wake.intervalSeconds, 3600U);
    EXPECT_EQ(wake.notificationNodeId, 1);
}

TEST(PolicySerialization, EmptyPolicyRoundTrips)
{
    const auto bytes     = PolicyRegister::serialize(Policy{});
    const auto roundtrip = PolicyRegister::deserialize(bytes);
    ASSERT_TRUE(roundtrip.has_value());
    EXPECT_TRUE(roundtrip->empty());
}

TEST(PolicySerialization, RejectsTruncatedAndUnknownBlobs)
{
    EXPECT_FALSE(PolicyRegister::deserialize({}).has_value());            // empty
    EXPECT_FALSE(PolicyRegister::deserialize({0xFF, 0x00}).has_value());  // wrong version
    // Version 1, claims 1 entry, but the body is cut off.
    EXPECT_FALSE(PolicyRegister::deserialize({0x01, 0x01, 0x01, 0x03}).has_value());
}

// ---- Merge precedence -----------------------------------------------

TEST(PolicyMerge, OverrideWinsPerSlotAndAppendsNew)
{
    Policy device;
    device.emplace_back(ConfigurationEntry{.parameter = 3, .size = 1, .isSigned = false, .value = 10});
    device.emplace_back(ConfigurationEntry{.parameter = 4, .size = 1, .isSigned = false, .value = 20});
    device.emplace_back(WakeUpEntry{.intervalSeconds = 3600, .notificationNodeId = 1});

    Policy override;
    override.emplace_back(ConfigurationEntry{.parameter = 4, .size = 1, .isSigned = false, .value = 99});  // replaces
    override.emplace_back(ConfigurationEntry{.parameter = 7, .size = 1, .isSigned = false, .value = 5});   // appends
    override.emplace_back(WakeUpEntry{.intervalSeconds = 600, .notificationNodeId = 1});                   // replaces

    const auto merged = PolicyRegister::merge(device, override);
    ASSERT_EQ(merged.size(), 4U);
    // param 3 kept from device; param 4 overridden in place; wake-up
    // overridden in its slot; param 7 appended at the end.
    EXPECT_EQ(std::get<ConfigurationEntry>(merged[0]).value, 10);  // param 3 untouched
    EXPECT_EQ(std::get<ConfigurationEntry>(merged[1]).value, 99);  // param 4 replaced
    EXPECT_EQ(std::get<WakeUpEntry>(merged[2]).intervalSeconds, 600U);
    EXPECT_EQ(std::get<ConfigurationEntry>(merged[3]).parameter, 7);  // appended
}

// ---- CRUD + persistence ---------------------------------------------

TEST_F(PolicyRegisterTest, DevicePolicyCrud)
{
    PolicyRegister::Register reg(dbPath_);
    EXPECT_FALSE(reg.devicePolicy(kDevice).has_value());

    Policy policy;
    policy.emplace_back(ConfigurationEntry{.parameter = 1, .size = 1, .isSigned = false, .value = 7});
    reg.setDevicePolicy(kDevice, policy);

    const auto fetched = reg.devicePolicy(kDevice);
    ASSERT_TRUE(fetched.has_value());
    ASSERT_EQ(fetched->size(), 1U);
    EXPECT_EQ(std::get<ConfigurationEntry>((*fetched)[0]).value, 7);

    reg.deleteDevicePolicy(kDevice);
    EXPECT_FALSE(reg.devicePolicy(kDevice).has_value());
}

TEST_F(PolicyRegisterTest, NodeOverrideRequiresHomeAndScopesToIt)
{
    PolicyRegister::Register reg(dbPath_);
    Policy policy;
    policy.emplace_back(AssociationEntry{.groupId = 1, .members = {1}});

    // No home bound yet — dropped.
    reg.setNodeOverride(5, policy);
    reg.setHomeId(kHomeId);
    EXPECT_FALSE(reg.nodeOverride(5).has_value());

    reg.setNodeOverride(5, policy);
    ASSERT_TRUE(reg.nodeOverride(5).has_value());

    // A different home doesn't see node 5's override.
    reg.setHomeId({0x11, 0x22, 0x33, 0x44});
    EXPECT_FALSE(reg.nodeOverride(5).has_value());
}

TEST_F(PolicyRegisterTest, EffectivePolicyMergesWhenIdentityKnown)
{
    PolicyRegister::Register reg(dbPath_);
    reg.setHomeId(kHomeId);

    Policy device;
    device.emplace_back(ConfigurationEntry{.parameter = 3, .size = 1, .isSigned = false, .value = 10});
    reg.setDevicePolicy(kDevice, device);

    Policy override;
    override.emplace_back(ConfigurationEntry{.parameter = 3, .size = 1, .isSigned = false, .value = 42});
    reg.setNodeOverride(9, override);

    // Without identity, only the override is visible.
    {
        const auto eff = reg.effectivePolicy(9);
        ASSERT_EQ(eff.size(), 1U);
        EXPECT_EQ(std::get<ConfigurationEntry>(eff[0]).value, 42);
    }

    // With identity, device default merges and the override wins.
    reg.noteDeviceIdentity(9, kDevice);
    const auto eff = reg.effectivePolicy(9);
    ASSERT_EQ(eff.size(), 1U);
    EXPECT_EQ(std::get<ConfigurationEntry>(eff[0]).value, 42);
}

TEST_F(PolicyRegisterTest, EffectivePolicyEmptyWhenNothingApplies)
{
    PolicyRegister::Register reg(dbPath_);
    reg.setHomeId(kHomeId);
    EXPECT_TRUE(reg.effectivePolicy(9).empty());
}

TEST_F(PolicyRegisterTest, ListsAllDevicePolicies)
{
    PolicyRegister::Register reg(dbPath_);
    EXPECT_TRUE(reg.listDevicePolicies().empty());

    Policy a;
    a.emplace_back(ConfigurationEntry{.parameter = 1, .size = 1, .isSigned = false, .value = 1});
    Policy b;
    b.emplace_back(AssociationEntry{.groupId = 1, .members = {1}});
    reg.setDevicePolicy(kDevice, a);
    reg.setDevicePolicy(DeviceId{.manufacturerId = 0x1234, .productTypeId = 0x5, .productId = 0x6}, b);

    const auto all = reg.listDevicePolicies();
    ASSERT_EQ(all.size(), 2U);
    // Order isn't guaranteed; find each by identity.
    bool sawKDevice = false;
    bool sawOther   = false;
    for (const auto& row : all)
    {
        if (row.device.manufacturerId == kDevice.manufacturerId && row.device.productId == kDevice.productId)
        {
            sawKDevice = true;
            ASSERT_EQ(row.policy.size(), 1U);
            EXPECT_EQ(std::get<ConfigurationEntry>(row.policy[0]).value, 1);
        }
        else if (row.device.manufacturerId == 0x1234)
        {
            sawOther = true;
            ASSERT_EQ(row.policy.size(), 1U);
            EXPECT_EQ(std::get<AssociationEntry>(row.policy[0]).groupId, 1);
        }
    }
    EXPECT_TRUE(sawKDevice);
    EXPECT_TRUE(sawOther);
}

TEST_F(PolicyRegisterTest, PersistsAcrossRestart)
{
    Policy device;
    device.emplace_back(WakeUpEntry{.intervalSeconds = 7200, .notificationNodeId = 1});
    Policy override;
    override.emplace_back(AssociationEntry{.groupId = 2, .members = {1, 3}});
    {
        PolicyRegister::Register first(dbPath_);
        first.setHomeId(kHomeId);
        first.setDevicePolicy(kDevice, device);
        first.setNodeOverride(5, override);
    }  // first's destructor closes the connection

    PolicyRegister::Register second(dbPath_);
    second.setHomeId(kHomeId);
    ASSERT_TRUE(second.devicePolicy(kDevice).has_value());
    EXPECT_EQ(std::get<WakeUpEntry>((*second.devicePolicy(kDevice))[0]).intervalSeconds, 7200U);
    ASSERT_TRUE(second.nodeOverride(5).has_value());
    EXPECT_EQ(std::get<AssociationEntry>((*second.nodeOverride(5))[0]).members, (std::vector<std::uint8_t>{1, 3}));
}
