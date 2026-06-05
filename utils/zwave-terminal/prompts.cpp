#include "prompts.hpp"

#include "constants.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <ncurses.h>

namespace zwt
{
/// Prompt at the bottom row for an integer in [minVal, maxVal]. Switches
/// ncurses to blocking echoing input, reads a line, parses it, then
/// restores the periodic-redraw input mode. Returns std::nullopt on
/// empty input, parse error, or out-of-range value.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): min/max are clearly named at call sites
auto promptByte(const char* label, int minVal, int maxVal) -> std::optional<std::uint8_t>
{
    const int rows = getmaxy(stdscr);
    move(rows - 1, 0);
    clrtoeol();
    mvprintw(rows - 1, 0, "%s ", label);
    refresh();

    echo();
    curs_set(1);
    timeout(-1);  // blocking

    std::array<char, NODE_ID_INPUT_BUFFER> buffer{};
    int const got = getnstr(buffer.data(), static_cast<int>(buffer.size()) - 1);

    noecho();
    curs_set(0);
    timeout(UI_REFRESH_MS);

    if (got != OK)
    {
        return std::nullopt;
    }

    const std::string text(buffer.data());
    if (text.empty())
    {
        return std::nullopt;
    }

    int value                   = 0;
    auto const [ptr, errorCode] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (errorCode != std::errc{} || value < minVal || value > maxVal)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

auto promptNodeId(const char* label) -> std::optional<std::uint8_t>
{
    return promptByte(label, NODE_ID_MIN, NODE_ID_MAX);
}

/// Like promptByte but for a signed 32-bit integer (Configuration values
/// are i32). Same blocking-echo prompt mechanics. Returns std::nullopt on
/// empty input, parse error, or out-of-i32-range value.
auto promptInt32(const char* label) -> std::optional<std::int32_t>
{
    const int rows = getmaxy(stdscr);
    move(rows - 1, 0);
    clrtoeol();
    mvprintw(rows - 1, 0, "%s ", label);
    refresh();

    echo();
    curs_set(1);
    timeout(-1);  // blocking

    std::array<char, INT_INPUT_BUFFER> buffer{};
    int const got = getnstr(buffer.data(), static_cast<int>(buffer.size()) - 1);

    noecho();
    curs_set(0);
    timeout(UI_REFRESH_MS);

    if (got != OK)
    {
        return std::nullopt;
    }
    const std::string text(buffer.data());
    if (text.empty())
    {
        return std::nullopt;
    }
    std::int32_t value          = 0;
    auto const [ptr, errorCode] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (errorCode != std::errc{})
    {
        return std::nullopt;
    }
    return value;
}

/// Blocking bottom-row line prompt. Returns the entered text, or nullopt
/// on empty input / read error. Shared by the typed prompts below.
auto promptLine(const char* label) -> std::optional<std::string>
{
    const int rows = getmaxy(stdscr);
    move(rows - 1, 0);
    clrtoeol();
    mvprintw(rows - 1, 0, "%s ", label);
    refresh();

    echo();
    curs_set(1);
    timeout(-1);  // blocking

    std::array<char, LINE_INPUT_BUFFER> buffer{};
    int const got = getnstr(buffer.data(), static_cast<int>(buffer.size()) - 1);

    noecho();
    curs_set(0);
    timeout(UI_REFRESH_MS);

    if (got != OK)
    {
        return std::nullopt;
    }
    std::string text(buffer.data());
    if (text.empty())
    {
        return std::nullopt;
    }
    return text;
}

/// Parse an unsigned integer, accepting a `0x` prefix for hex. nullopt on
/// trailing junk or out-of-range.
auto parseUint(const std::string& text, std::uint32_t maxVal) -> std::optional<std::uint32_t>
{
    const char* begin = text.data();
    const char* end   = begin + text.size();
    int base          = DECIMAL_BASE;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        begin += 2;
        base = HEX_BASE;
    }
    std::uint32_t value         = 0;
    auto const [ptr, errorCode] = std::from_chars(begin, end, value, base);
    if (errorCode != std::errc{} || ptr != end || value > maxVal)
    {
        return std::nullopt;
    }
    return value;
}

auto promptU16(const char* label) -> std::optional<std::uint16_t>
{
    auto text = promptLine(label);
    if (!text.has_value())
    {
        return std::nullopt;
    }
    auto value = parseUint(*text, U16_MAX);
    if (!value.has_value())
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(*value);
}

auto promptU32(const char* label) -> std::optional<std::uint32_t>
{
    auto text = promptLine(label);
    if (!text.has_value())
    {
        return std::nullopt;
    }
    return parseUint(*text, U32_MAX);
}

/// Read a single character from a line prompt, restricted to `valid`.
auto promptChar(const char* label, const std::string& valid) -> std::optional<char>
{
    auto text = promptLine(label);
    if (!text.has_value())
    {
        return std::nullopt;
    }
    const char chosen = (*text)[0];
    if (valid.find(chosen) == std::string::npos)
    {
        return std::nullopt;
    }
    return chosen;
}

/// Parse a space/comma-separated list of node IDs (each 1..232). Requires
/// at least one. nullopt on any malformed / out-of-range token.
auto promptNodeList(const char* label) -> std::optional<std::vector<std::uint8_t>>
{
    auto text = promptLine(label);
    if (!text.has_value())
    {
        return std::nullopt;
    }
    for (auto& character : *text)
    {
        if (character == ',')
        {
            character = ' ';
        }
    }
    std::istringstream stream(*text);
    std::vector<std::uint8_t> members;
    int member = 0;
    while (stream >> member)
    {
        if (member < NODE_ID_MIN || member > NODE_ID_MAX)
        {
            return std::nullopt;
        }
        members.push_back(static_cast<std::uint8_t>(member));
    }
    if (!stream.eof() || members.empty())
    {
        return std::nullopt;
    }
    return members;
}
}  // namespace zwt
