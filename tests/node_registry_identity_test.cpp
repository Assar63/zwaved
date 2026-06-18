// Device-identity persistence on NodeRegistry (#203): the interview's
// ManufacturerSpecificReport is recorded into the manufacturer/product-type/
// product-id columns (schema v4) and surfaced via snapshot().

#include "MessageBus.hpp"
#include "NodeRegistry.hpp"

#include <cstdint>
#include <filesystem>

#include <gtest/gtest.h>

namespace
{
auto useTempStateDir() -> void
{
    const auto dir = std::filesystem::temp_directory_path() / "zwaved_nodereg_identity_test";
    std::filesystem::create_directories(dir);
    MessageBus::publish(MessageBus::StorageConfig{.stateDir = dir.string()});
}

auto identityOf(std::uint8_t nodeId) -> NodeRegistry::NodeInfo
{
    for (const auto& node : NodeRegistry::snapshot())
    {
        if (node.nodeId == nodeId)
        {
            return node;
        }
    }
    return {};
}
}  // namespace

TEST(NodeRegistryIdentity, SetAndQueryIdentity)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xCA, 0xFE, 0xF0, 0x0D});
    NodeRegistry::NodeInfo node;
    node.nodeId = 14;
    NodeRegistry::add(node);

    // Default: zeroed until the interview learns it.
    EXPECT_EQ(identityOf(14).manufacturerId, 0U);

    NodeRegistry::setDeviceIdentity(14, 0x0086, 0x0002, 0x0064);
    const auto info = identityOf(14);
    EXPECT_EQ(info.manufacturerId, 0x0086U);
    EXPECT_EQ(info.productTypeId, 0x0002U);
    EXPECT_EQ(info.productId, 0x0064U);
}

TEST(NodeRegistryIdentity, UnknownNodeIsNoOp)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xCA, 0xFE, 0xF0, 0x0D});
    NodeRegistry::setDeviceIdentity(200, 0x1234, 0x5678, 0x9ABC);  // never added
    EXPECT_EQ(identityOf(200).manufacturerId, 0U);
}

TEST(NodeRegistryIdentity, ManufacturerSpecificReportRecordsIdentity)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xCA, 0xFE, 0xF0, 0x0D});
    NodeRegistry::NodeInfo node;
    node.nodeId = 15;
    NodeRegistry::add(node);

    MessageBus::publish(MessageBus::ManufacturerSpecificReport{
        .sourceNodeId = 15, .manufacturerId = 0x0271, .productTypeId = 0x0003, .productId = 0x0084});

    const auto info = identityOf(15);
    EXPECT_EQ(info.manufacturerId, 0x0271U);
    EXPECT_EQ(info.productTypeId, 0x0003U);
    EXPECT_EQ(info.productId, 0x0084U);
}
