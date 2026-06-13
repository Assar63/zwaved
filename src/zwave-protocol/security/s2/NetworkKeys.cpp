#include "NetworkKeys.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <openssl/rand.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{
constexpr const char* DEFAULT_STATE_DIR = "/var/lib/zwaved";
constexpr const char* STATE_DIR_ENV     = "ZWAVED_STATE_DIR";
constexpr int KEY_FILE_MODE             = 0600;  // owner read/write only

// Read exactly one key from `path`; false on open / short / long read.
auto readKey(const std::filesystem::path& path, S2::Crypto::Key& out) -> bool
{
    std::error_code errc;
    if (std::filesystem::file_size(path, errc) != out.size() || errc)
    {
        return false;
    }
    const int keyFd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (keyFd < 0)
    {
        return false;
    }
    const ssize_t got = ::read(keyFd, out.data(), out.size());
    ::close(keyFd);
    return got == static_cast<ssize_t>(out.size());
}

// Create `path` exclusively (0600) and write `key`; false if it already exists
// (lost a race) or on any I/O error.
auto writeKeyExclusive(const std::filesystem::path& path, const S2::Crypto::Key& key) -> bool
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): open()'s mode arg is the documented variadic
    const int keyFd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, KEY_FILE_MODE);
    if (keyFd < 0)
    {
        return false;
    }
    const ssize_t put = ::write(keyFd, key.data(), key.size());
    ::close(keyFd);
    if (put != static_cast<ssize_t>(key.size()))
    {
        std::error_code errc;
        std::filesystem::remove(path, errc);
        return false;
    }
    return true;
}

// Load one key at `path`, or generate + persist it. Returns the key and whether
// it was freshly created; std::nullopt on any I/O / RNG failure.
struct OneKey
{
    S2::Crypto::Key key{};
    bool generated = false;
};
auto loadOrGenerateOne(const std::filesystem::path& path) -> std::optional<OneKey>
{
    OneKey result;
    if (readKey(path, result.key))
    {
        return result;
    }
    if (RAND_bytes(result.key.data(), static_cast<int>(result.key.size())) != 1)
    {
        return std::nullopt;
    }
    if (!writeKeyExclusive(path, result.key))
    {
        // Lost a create race — re-read whatever is now there.
        if (readKey(path, result.key))
        {
            return result;
        }
        return std::nullopt;
    }
    result.generated = true;
    return result;
}
}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): s2KeyDir-then-stateDir matches the config field order
auto S2::NetworkKeys::resolveDir(const std::string& s2KeyDir, const std::string& stateDir) -> std::filesystem::path
{
    if (!s2KeyDir.empty())
    {
        return s2KeyDir;
    }
    std::string dir = stateDir;
    if (dir.empty())
    {
        // NOLINTNEXTLINE(concurrency-mt-unsafe): runs during single-threaded startup
        if (const char* env = std::getenv(STATE_DIR_ENV); env != nullptr && *env != '\0')
        {
            dir = env;
        }
        else
        {
            dir = DEFAULT_STATE_DIR;
        }
    }
    return std::filesystem::path(dir) / "security" / "s2";
}

auto S2::NetworkKeys::fileName(Class cls) -> std::string
{
    switch (cls)
    {
    case Class::Unauthenticated:
        return "unauth.key";
    case Class::Authenticated:
        return "auth.key";
    case Class::AccessControl:
        return "access.key";
    case Class::S0Compat:
        return "s0compat.key";
    }
    return "unknown.key";
}

auto S2::NetworkKeys::loadOrGenerateAll(const std::filesystem::path& dir) -> std::optional<Loaded>
{
    std::error_code errc;
    std::filesystem::create_directories(dir, errc);
    if (errc)
    {
        return std::nullopt;
    }
    Loaded loaded;
    for (std::size_t i = 0; i < CLASS_COUNT; ++i)
    {
        const auto one = loadOrGenerateOne(dir / fileName(static_cast<Class>(i)));
        if (!one.has_value())
        {
            return std::nullopt;
        }
        loaded.keys.at(i)      = one->key;
        loaded.generated.at(i) = one->generated;
    }
    return loaded;
}

auto S2::NetworkKeys::keyFor(const KeySet& keys, Class cls) -> const Crypto::Key&
{
    return keys.at(static_cast<std::size_t>(cls));
}

namespace
{
auto keySlot() -> std::optional<S2::NetworkKeys::KeySet>&
{
    static std::optional<S2::NetworkKeys::KeySet> slot;
    return slot;
}
}  // namespace

auto S2::NetworkKeys::current() -> std::optional<KeySet>
{
    return keySlot();
}

auto S2::NetworkKeys::setCurrent(const KeySet& keys) -> void
{
    keySlot() = keys;
}
