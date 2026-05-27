#include "Config.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr const char* DEFAULT_PATH = "/etc/zwaved/zwaved.conf";
constexpr const char* PATH_ENV     = "ZWAVED_CONFIG";

constexpr std::uint8_t LEVEL_DEBUG = 0;
constexpr std::uint8_t LEVEL_INFO  = 1;
constexpr std::uint8_t LEVEL_WARN  = 2;
constexpr std::uint8_t LEVEL_ERROR = 3;

auto trim(std::string_view text) -> std::string_view
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    {
        text.remove_suffix(1);
    }
    return text;
}

/// One section's entries, in file order. Keys may repeat —
/// `dongles.accept` uses that to express a list.
using Section    = std::vector<std::pair<std::string, std::string>>;
using ParsedFile = std::map<std::string, Section>;

/// Parse the file. Format:
///   # comments
///   [section]
///   key = value
/// Whitespace around keys/values is stripped. Surrounding double
/// quotes on a value are stripped. Lines without `=` outside a
/// section header are warned-about and skipped.
auto parseFile(const std::filesystem::path& path) -> std::optional<ParsedFile>
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        return std::nullopt;
    }

    ParsedFile result;
    std::string section;
    std::string line;
    int lineno = 0;
    while (std::getline(input, line))
    {
        ++lineno;
        const auto trimmed = std::string(trim(line));
        if (trimmed.empty() || trimmed.front() == '#')
        {
            continue;
        }
        if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
        {
            section = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }

        const auto equalsPos = trimmed.find('=');
        if (equalsPos == std::string::npos)
        {
            Logger::warn("Config: line " + std::to_string(lineno) + " has no '=', skipped");
            continue;
        }

        std::string key   = std::string(trim(std::string_view(trimmed).substr(0, equalsPos)));
        std::string value = std::string(trim(std::string_view(trimmed).substr(equalsPos + 1)));

        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        {
            value = value.substr(1, value.size() - 2);
        }

        result[section].emplace_back(std::move(key), std::move(value));
    }
    return result;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): section-then-key matches the file order at every call site
auto firstValue(const ParsedFile& file,
                const std::string& section,
                const std::string& key) -> std::optional<std::string>
{
    const auto iter = file.find(section);
    if (iter == file.end())
    {
        return std::nullopt;
    }
    for (const auto& [entryKey, entryValue] : iter->second)
    {
        if (entryKey == key)
        {
            return entryValue;
        }
    }
    return std::nullopt;
}

auto allValues(const ParsedFile& file, const std::string& section, const std::string& key) -> std::vector<std::string>
{
    const auto iter = file.find(section);
    if (iter == file.end())
    {
        return {};
    }
    std::vector<std::string> out;
    for (const auto& [entryKey, entryValue] : iter->second)
    {
        if (entryKey == key)
        {
            out.push_back(entryValue);
        }
    }
    return out;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

auto parseLevel(std::string_view text) -> std::optional<std::uint8_t>
{
    if (text == "debug")
    {
        return LEVEL_DEBUG;
    }
    if (text == "info")
    {
        return LEVEL_INFO;
    }
    if (text == "warn")
    {
        return LEVEL_WARN;
    }
    if (text == "error")
    {
        return LEVEL_ERROR;
    }
    return std::nullopt;
}

auto parseBool(std::string_view text) -> std::optional<bool>
{
    if (text == "true" || text == "yes" || text == "1")
    {
        return true;
    }
    if (text == "false" || text == "no" || text == "0")
    {
        return false;
    }
    return std::nullopt;
}

/// Parse one `accept = <vid>:<pid>:<name>` value. The name field is
/// free-form and may itself contain colons — only the first two
/// colons are treated as separators.
auto parseAccept(std::string_view text) -> std::optional<MessageBus::AcceptedDongleConfig>
{
    const auto first = text.find(':');
    if (first == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto second = text.find(':', first + 1);
    if (second == std::string_view::npos)
    {
        return std::nullopt;
    }
    MessageBus::AcceptedDongleConfig accepted;
    accepted.vid  = std::string(trim(text.substr(0, first)));
    accepted.pid  = std::string(trim(text.substr(first + 1, second - first - 1)));
    accepted.name = std::string(trim(text.substr(second + 1)));
    if (accepted.vid.empty() || accepted.pid.empty())
    {
        return std::nullopt;
    }
    return accepted;
}

auto resolvePath() -> std::filesystem::path
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe): runs once during the priority-102 constructor
    if (const char* env = std::getenv(PATH_ENV); env != nullptr && *env != '\0')
    {
        return env;
    }
    return DEFAULT_PATH;
}

auto loadLoggerSection(const ParsedFile& file, MessageBus::LoggerConfig& logger) -> void
{
    const auto value = firstValue(file, "logger", "min_level");
    if (!value.has_value())
    {
        return;
    }
    if (const auto level = parseLevel(*value); level.has_value())
    {
        logger.minLevel = *level;
    }
    else
    {
        Logger::warn("Config: unknown logger.min_level '" + *value + "', keeping default");
    }
}

auto loadStorageSection(const ParsedFile& file, MessageBus::StorageConfig& storage) -> void
{
    if (const auto value = firstValue(file, "storage", "state_dir"); value.has_value())
    {
        storage.stateDir = *value;
    }
}

auto loadDonglesSection(const ParsedFile& file, MessageBus::DonglesConfig& dongles) -> void
{
    const auto entries = allValues(file, "dongles", "accept");
    if (entries.empty())
    {
        return;
    }
    std::vector<MessageBus::AcceptedDongleConfig> parsedList;
    for (const auto& entry : entries)
    {
        if (auto accepted = parseAccept(entry); accepted.has_value())
        {
            parsedList.push_back(*accepted);
        }
        else
        {
            Logger::warn("Config: malformed dongles.accept '" + entry + "', skipped");
        }
    }
    if (!parsedList.empty())
    {
        dongles.accept = std::move(parsedList);
    }
}

auto loadBehaviorSection(const ParsedFile& file, MessageBus::BehaviorConfig& behavior) -> void
{
    const auto value = firstValue(file, "behavior", "auto_lifeline");
    if (!value.has_value())
    {
        return;
    }
    if (const auto flag = parseBool(*value); flag.has_value())
    {
        behavior.autoLifeline = *flag;
    }
    else
    {
        Logger::warn("Config: unknown behavior.auto_lifeline '" + *value + "', keeping default");
    }
}
}  // namespace

auto Config::load() -> void
{
    // Defaults reflect the daemon's pre-config baseline. These values
    // are published even when no config file is present, so every
    // subscriber sees a value on subscribe.
    MessageBus::LoggerConfig logger{.minLevel = LEVEL_INFO};
    MessageBus::StorageConfig storage{};
    MessageBus::DonglesConfig dongles{
        .accept = {MessageBus::AcceptedDongleConfig{.vid = "0658", .pid = "0200", .name = "Aeotec Z-Stick Gen5"}}};
    MessageBus::BehaviorConfig behavior{.autoLifeline = true};

    const auto path = resolvePath();
    if (!std::filesystem::exists(path))
    {
        Logger::info("Config: no config file at " + path.string() + " — publishing defaults");
    }
    else if (const auto parsed = parseFile(path); !parsed.has_value())
    {
        Logger::error("Config: failed to open " + path.string() + " — publishing defaults");
        MessageBus::publish(MessageBus::DaemonError{
            .severity = MessageBus::DaemonError::SEVERITY_WARN,
            .source   = "config",
            .code     = MessageBus::DaemonError::CODE_CONFIG_LOAD_FAILED,
            .message  = "failed to open config file " + path.string() + "; using defaults",
        });
    }
    else
    {
        const auto& file = *parsed;
        loadLoggerSection(file, logger);
        loadStorageSection(file, storage);
        loadDonglesSection(file, dongles);
        loadBehaviorSection(file, behavior);
        Logger::info("Config: loaded " + path.string());
    }

    MessageBus::publish(logger);
    MessageBus::publish(storage);
    MessageBus::publish(dongles);
    MessageBus::publish(behavior);
}

namespace
{
/// Constructor priority 102 — runs after Logger (101) and the bus
/// touch the Logger constructor performs, so `Logger::info` calls
/// during parsing are queued correctly. Higher-priority constructors
/// (201+) subscribe to the resulting bus events and pick up the
/// retained values via replay-on-subscribe.
__attribute__((constructor(CONFIG_CONFIG_PRIO))) auto loadConfig() -> void
{
    Config::load();
}
}  // namespace
