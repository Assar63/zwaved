#ifndef ZWAVE_TERMINAL_RENDER_HPP
#define ZWAVE_TERMINAL_RENDER_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ncurses rendering for the zwave-terminal client: the main screen
// (`draw`) and the modal submenu overlay (`runActionMenu`). See #111.
namespace zwt
{
// One entry in a modal submenu: a trigger key, a label, and the action to
// run when chosen.
struct MenuItem
{
    char key;
    std::string label;
    std::function<void()> action;
};

// Render a modal submenu overlay, block for a key, and run the matching
// action. Any unmatched key (including Esc) cancels. Keeps each submenu's
// key namespace independent of the top-level menu, so adding a CC is just
// appending an item — no more single-key exhaustion (#109).
auto runActionMenu(const char* title, const std::vector<MenuItem>& items) -> void;

// Render a full-screen modal showing `title` + each line of `lines`, then
// block until a key is pressed. The read-only counterpart to runActionMenu —
// used by the per-node info drill-down (#45). Long lines are clipped to the
// screen width; lines past the bottom are dropped.
auto runInfoModal(const std::string& title, const std::vector<std::string>& lines) -> void;

auto draw(std::uint8_t lastSession) -> void;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_RENDER_HPP
