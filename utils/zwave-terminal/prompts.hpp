#ifndef ZWAVE_TERMINAL_PROMPTS_HPP
#define ZWAVE_TERMINAL_PROMPTS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Bottom-row modal input prompts for the zwave-terminal client. Each
// switches ncurses to blocking echoing input, reads a line, parses it, and
// restores the periodic-redraw mode. See #111.
namespace zwt
{
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): min/max are clearly named at call sites
[[nodiscard]] auto promptByte(const char* label, int minVal, int maxVal) -> std::optional<std::uint8_t>;
[[nodiscard]] auto promptNodeId(const char* label) -> std::optional<std::uint8_t>;
[[nodiscard]] auto promptInt32(const char* label) -> std::optional<std::int32_t>;
[[nodiscard]] auto promptLine(const char* label) -> std::optional<std::string>;
[[nodiscard]] auto parseUint(const std::string& text, std::uint32_t maxVal) -> std::optional<std::uint32_t>;
[[nodiscard]] auto promptU16(const char* label) -> std::optional<std::uint16_t>;
[[nodiscard]] auto promptU32(const char* label) -> std::optional<std::uint32_t>;
[[nodiscard]] auto promptChar(const char* label, const std::string& valid) -> std::optional<char>;
[[nodiscard]] auto promptNodeList(const char* label) -> std::optional<std::vector<std::uint8_t>>;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_PROMPTS_HPP
