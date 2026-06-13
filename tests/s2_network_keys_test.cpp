// Security S2 (CC 0x9F) multi-class network-key persistence (#180).

#include "NetworkKeys.hpp"

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace
{
namespace fs = std::filesystem;
using S2::NetworkKeys::Class;

class S2NetworkKeysTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        dir_ = fs::temp_directory_path() / ("zwaved_s2keys_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(dir_);
    }
    void TearDown() override
    {
        std::error_code errc;
        fs::remove_all(dir_, errc);
    }
    fs::path dir_;
};
}  // namespace

TEST_F(S2NetworkKeysTest, GeneratesAllFourOnFirstUse)
{
    const auto loaded = S2::NetworkKeys::loadOrGenerateAll(dir_);
    ASSERT_TRUE(loaded.has_value());
    for (std::size_t i = 0; i < S2::NetworkKeys::CLASS_COUNT; ++i)
    {
        EXPECT_TRUE(loaded->generated.at(i));
        const auto path = dir_ / S2::NetworkKeys::fileName(static_cast<Class>(i));
        EXPECT_TRUE(fs::exists(path));
        EXPECT_EQ(fs::file_size(path), S2::Crypto::KEY_SIZE);
    }
}

TEST_F(S2NetworkKeysTest, PersistsWith0600Permissions)
{
    ASSERT_TRUE(S2::NetworkKeys::loadOrGenerateAll(dir_).has_value());
    const auto perms = fs::status(dir_ / S2::NetworkKeys::fileName(Class::AccessControl)).permissions();
    EXPECT_EQ(perms & fs::perms::group_all, fs::perms::none);
    EXPECT_EQ(perms & fs::perms::others_all, fs::perms::none);
    EXPECT_NE(perms & fs::perms::owner_read, fs::perms::none);
}

TEST_F(S2NetworkKeysTest, LoadsExistingUnchanged)
{
    const auto first = S2::NetworkKeys::loadOrGenerateAll(dir_);
    ASSERT_TRUE(first.has_value());
    const auto second = S2::NetworkKeys::loadOrGenerateAll(dir_);
    ASSERT_TRUE(second.has_value());
    for (std::size_t i = 0; i < S2::NetworkKeys::CLASS_COUNT; ++i)
    {
        EXPECT_FALSE(second->generated.at(i));  // read from disk, not regenerated
        EXPECT_EQ(first->keys.at(i), second->keys.at(i));
    }
}

TEST_F(S2NetworkKeysTest, ClassKeysAreDistinct)
{
    const auto loaded = S2::NetworkKeys::loadOrGenerateAll(dir_);
    ASSERT_TRUE(loaded.has_value());
    std::set<S2::Crypto::Key> unique(loaded->keys.begin(), loaded->keys.end());
    EXPECT_EQ(unique.size(), S2::NetworkKeys::CLASS_COUNT);  // all four differ
}

TEST(S2NetworkKeysResolveDir, ExplicitDirWins)
{
    EXPECT_EQ(S2::NetworkKeys::resolveDir("/etc/zwaved/keys/s2", "/var/lib/zwaved"), fs::path("/etc/zwaved/keys/s2"));
}

TEST(S2NetworkKeysResolveDir, DefaultsUnderStateDir)
{
    EXPECT_EQ(S2::NetworkKeys::resolveDir("", "/srv/state"), fs::path("/srv/state/security/s2"));
}

TEST(S2NetworkKeysFileName, OnePerClass)
{
    EXPECT_EQ(S2::NetworkKeys::fileName(Class::Unauthenticated), "unauth.key");
    EXPECT_EQ(S2::NetworkKeys::fileName(Class::Authenticated), "auth.key");
    EXPECT_EQ(S2::NetworkKeys::fileName(Class::AccessControl), "access.key");
    EXPECT_EQ(S2::NetworkKeys::fileName(Class::S0Compat), "s0compat.key");
}
