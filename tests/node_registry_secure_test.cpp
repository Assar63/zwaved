// Security S0 `secure` flag on NodeRegistry (#167): set / query / unknown-node.

#include "MessageBus.hpp"
#include "NodeRegistry.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace
{
auto useTempStateDir() -> void
{
    const auto dir = std::filesystem::temp_directory_path() / "zwaved_nodereg_secure_test";
    std::filesystem::create_directories(dir);
    MessageBus::publish(MessageBus::StorageConfig{.stateDir = dir.string()});
}
}  // namespace

TEST(NodeRegistrySecure, SetAndQuery)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xDE, 0xAD, 0xBE, 0xEF});
    NodeRegistry::NodeInfo node;
    node.nodeId = 7;
    NodeRegistry::add(node);

    EXPECT_FALSE(NodeRegistry::isSecure(7));  // default
    NodeRegistry::setSecure(7, true);
    EXPECT_TRUE(NodeRegistry::isSecure(7));
    NodeRegistry::setSecure(7, false);
    EXPECT_FALSE(NodeRegistry::isSecure(7));
}

TEST(NodeRegistrySecure, UnknownNodeIsNeverSecure)
{
    useTempStateDir();
    NodeRegistry::setHomeId({0xDE, 0xAD, 0xBE, 0xEF});
    EXPECT_FALSE(NodeRegistry::isSecure(222));  // never added
    NodeRegistry::setSecure(223, true);         // no-op on an unknown node
    EXPECT_FALSE(NodeRegistry::isSecure(223));
}
