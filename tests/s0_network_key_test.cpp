#include "NetworkKey.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace
{
namespace fs = std::filesystem;

// A unique temp directory removed when the fixture is torn down.
class S0NetworkKeyTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        dir_ = fs::temp_directory_path() /
               ("zwaved_s0key_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)));
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

TEST_F(S0NetworkKeyTest, GeneratesOnFirstUse)
{
    const auto path   = dir_ / "s0.key";
    const auto loaded = S0::NetworkKey::loadOrGenerate(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->generated);
    EXPECT_TRUE(fs::exists(path));
    EXPECT_EQ(fs::file_size(path), S0::Crypto::KEY_SIZE);
}

TEST_F(S0NetworkKeyTest, PersistsWith0600Permissions)
{
    const auto path = dir_ / "s0.key";
    ASSERT_TRUE(S0::NetworkKey::loadOrGenerate(path).has_value());
    const auto perms = fs::status(path).permissions();
    EXPECT_EQ(perms & fs::perms::group_all, fs::perms::none);
    EXPECT_EQ(perms & fs::perms::others_all, fs::perms::none);
    EXPECT_NE(perms & fs::perms::owner_read, fs::perms::none);
    EXPECT_NE(perms & fs::perms::owner_write, fs::perms::none);
}

TEST_F(S0NetworkKeyTest, LoadsExistingKeyUnchanged)
{
    const auto path  = dir_ / "s0.key";
    const auto first = S0::NetworkKey::loadOrGenerate(path);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(first->generated);

    const auto second = S0::NetworkKey::loadOrGenerate(path);
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(second->generated);  // read from disk, not regenerated
    EXPECT_EQ(first->key, second->key);
}

TEST_F(S0NetworkKeyTest, FreshKeysDifferBetweenPaths)
{
    const auto one = S0::NetworkKey::loadOrGenerate(dir_ / "a.key");
    const auto two = S0::NetworkKey::loadOrGenerate(dir_ / "b.key");
    ASSERT_TRUE(one.has_value());
    ASSERT_TRUE(two.has_value());
    EXPECT_NE(one->key, two->key);  // RAND_bytes makes a collision astronomically unlikely
}

TEST_F(S0NetworkKeyTest, CreatesMissingParentDirectories)
{
    const auto path   = dir_ / "nested" / "deeper" / "s0.key";
    const auto loaded = S0::NetworkKey::loadOrGenerate(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(fs::exists(path));
}

TEST(S0NetworkKeyResolvePath, ExplicitKeyFileWins)
{
    EXPECT_EQ(S0::NetworkKey::resolvePath("/etc/zwaved/keys/s0.key", "/var/lib/zwaved"),
              fs::path("/etc/zwaved/keys/s0.key"));
}

TEST(S0NetworkKeyResolvePath, DefaultsUnderStateDir)
{
    EXPECT_EQ(S0::NetworkKey::resolvePath("", "/srv/state"), fs::path("/srv/state/security/s0.key"));
}
