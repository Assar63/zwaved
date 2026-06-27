// Interview-capability persistence on NodeRegistry (#203): the interview's
// Version (CC 0x86), Multi Channel End Point (CC 0x60), and Z-Wave Plus Info
// (CC 0x5E) reports are recorded into the schema-v5 capability columns and
// surfaced via snapshot().

#include "MessageBus.hpp"
#include "NodeRegistry.hpp"

#include <cstdint>
#include <filesystem>

#include <gtest/gtest.h>

namespace
{
auto useTempStateDir() -> void
{
    const auto dir = std::filesystem::temp_directory_path() / "zwaved_nodereg_caps_test";
    std::filesystem::create_directories(dir);
    MessageBus::publish(MessageBus::StorageConfig{.stateDir = dir.string()});
}

auto infoOf(std::uint8_t nodeId) -> NodeRegistry::NodeInfo
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

TEST(NodeRegistryCapabilities, SetAndQueryVersion)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xCA, 0xFE, 0xBE, 0xEF});
    NodeRegistry::NodeInfo node;
    node.nodeId = 20;
    NodeRegistry::add(node);

    EXPECT_EQ(infoOf(20).applicationVersion, 0U);  // zeroed until gathered

    NodeRegistry::setVersionInfo(20, 0x03, 0x07, 0x0B, 0x01, 0x02);
    const auto info = infoOf(20);
    EXPECT_EQ(info.libraryType, 0x03U);
    EXPECT_EQ(info.protocolVersion, 0x07U);
    EXPECT_EQ(info.protocolSubVersion, 0x0BU);
    EXPECT_EQ(info.applicationVersion, 0x01U);
    EXPECT_EQ(info.applicationSubVersion, 0x02U);
}

TEST(NodeRegistryCapabilities, SetAndQueryEndpoints)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xCA, 0xFE, 0xBE, 0xEF});
    NodeRegistry::NodeInfo node;
    node.nodeId = 21;
    NodeRegistry::add(node);

    NodeRegistry::setEndpointInfo(21, 4, /*dynamic=*/true, /*identical=*/false);
    const auto info = infoOf(21);
    EXPECT_EQ(info.endpointCount, 4U);
    EXPECT_TRUE(info.endpointsDynamic);
    EXPECT_FALSE(info.endpointsIdentical);
}

TEST(NodeRegistryCapabilities, SetAndQueryZWavePlus)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xCA, 0xFE, 0xBE, 0xEF});
    NodeRegistry::NodeInfo node;
    node.nodeId = 22;
    NodeRegistry::add(node);

    NodeRegistry::setZWavePlusInfo(22, 0x02, 0x05, 0x00, 0x0700, 0x0701);
    const auto info = infoOf(22);
    EXPECT_EQ(info.zwavePlusVersion, 0x02U);
    EXPECT_EQ(info.roleType, 0x05U);
    EXPECT_EQ(info.nodeType, 0x00U);
    EXPECT_EQ(info.installerIconType, 0x0700U);
    EXPECT_EQ(info.userIconType, 0x0701U);
}

TEST(NodeRegistryCapabilities, UnknownNodeIsNoOp)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xCA, 0xFE, 0xBE, 0xEF});
    NodeRegistry::setVersionInfo(201, 0x03, 0x07, 0x0B, 0x01, 0x02);  // never added
    NodeRegistry::setEndpointInfo(201, 4, true, false);
    NodeRegistry::setZWavePlusInfo(201, 0x02, 0x05, 0x00, 0x0700, 0x0701);
    EXPECT_EQ(infoOf(201).applicationVersion, 0U);
    EXPECT_EQ(infoOf(201).endpointCount, 0U);
    EXPECT_EQ(infoOf(201).roleType, 0U);
}

TEST(NodeRegistryCapabilities, ReportsRecordCapabilities)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xCA, 0xFE, 0xBE, 0xEF});
    NodeRegistry::NodeInfo node;
    node.nodeId = 23;
    NodeRegistry::add(node);

    MessageBus::publish(MessageBus::NodeVersionReport{.sourceNodeId          = 23,
                                                      .libraryType           = 0x06,
                                                      .protocolVersion       = 0x04,
                                                      .protocolSubVersion    = 0x05,
                                                      .applicationVersion    = 0x01,
                                                      .applicationSubVersion = 0x00});
    MessageBus::publish(MessageBus::MultiChannelEndPointReport{
        .sourceNodeId = 23, .endpointCount = 2, .dynamic = false, .identical = true});
    MessageBus::publish(MessageBus::ZWavePlusInfoReport{.sourceNodeId      = 23,
                                                        .zwavePlusVersion  = 0x02,
                                                        .roleType          = 0x06,
                                                        .nodeType          = 0x00,
                                                        .installerIconType = 0x0A00,
                                                        .userIconType      = 0x0A01});

    const auto info = infoOf(23);
    EXPECT_EQ(info.libraryType, 0x06U);
    EXPECT_EQ(info.applicationVersion, 0x01U);
    EXPECT_EQ(info.endpointCount, 2U);
    EXPECT_TRUE(info.endpointsIdentical);
    EXPECT_EQ(info.roleType, 0x06U);
    EXPECT_EQ(info.installerIconType, 0x0A00U);
}
