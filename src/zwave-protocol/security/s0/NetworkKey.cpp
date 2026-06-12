#include "NetworkKey.hpp"

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

// Read exactly the whole file into `out`; false on open/short/long read.
auto readKeyFile(const std::filesystem::path& path, S0::Crypto::Key& out) -> bool
{
    std::error_code errc;
    const auto size = std::filesystem::file_size(path, errc);
    if (errc || size != out.size())
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

// Create `path` exclusively (0600) and write `key`; false if it already
// exists (lost a race — caller re-reads) or on any I/O error.
auto writeKeyFileExclusive(const std::filesystem::path& path, const S0::Crypto::Key& key) -> bool
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
        std::filesystem::remove(path, errc);  // don't leave a truncated key behind
        return false;
    }
    return true;
}
}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): s0KeyFile-then-stateDir matches the config field order
auto S0::NetworkKey::resolvePath(const std::string& s0KeyFile, const std::string& stateDir) -> std::filesystem::path
{
    if (!s0KeyFile.empty())
    {
        return s0KeyFile;
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
    return std::filesystem::path(dir) / "security" / "s0.key";
}

auto S0::NetworkKey::loadOrGenerate(const std::filesystem::path& path) -> std::optional<Loaded>
{
    Crypto::Key key{};
    if (readKeyFile(path, key))
    {
        return Loaded{.key = key, .generated = false};
    }

    // Absent (or unreadable) — generate and persist.
    std::error_code errc;
    std::filesystem::create_directories(path.parent_path(), errc);
    if (errc)
    {
        return std::nullopt;
    }
    if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1)
    {
        return std::nullopt;
    }
    if (!writeKeyFileExclusive(path, key))
    {
        // Another instance may have created it between our read and write —
        // try once more to read what's now there.
        Crypto::Key raced{};
        if (readKeyFile(path, raced))
        {
            return Loaded{.key = raced, .generated = false};
        }
        return std::nullopt;
    }
    return Loaded{.key = key, .generated = true};
}

namespace
{
// Function-local static (not a namespace-scope global): set once at startup,
// read on the bus thread thereafter.
auto keySlot() -> std::optional<S0::Crypto::Key>&
{
    static std::optional<S0::Crypto::Key> slot;
    return slot;
}
}  // namespace

auto S0::NetworkKey::current() -> std::optional<Crypto::Key>
{
    return keySlot();
}

auto S0::NetworkKey::setCurrent(const Crypto::Key& key) -> void
{
    keySlot() = key;
}
