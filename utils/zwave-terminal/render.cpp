#include "render.hpp"

#include "activity.hpp"
#include "constants.hpp"
#include "format.hpp"
#include "nodes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include <ncurses.h>

namespace zwt
{
namespace
{
constexpr int LIST_WIDTH_MAX = 36;  // node-list pane width cap
constexpr int LOG_ROWS       = 6;   // activity-log rows at the foot

constexpr std::uint8_t CC_SECURITY_2 = 0x9F;
constexpr std::uint8_t CC_SECURITY_0 = 0x98;

constexpr std::int64_t SECONDS_PER_MINUTE = 60;
constexpr std::int64_t SECONDS_PER_HOUR   = SECONDS_PER_MINUTE * 60;
constexpr std::int64_t SECONDS_PER_DAY    = SECONDS_PER_HOUR * 24;

constexpr std::uint8_t LOW_NIBBLE_MASK = 0x0F;
constexpr int NIBBLE_BITS              = 4;

// Node-list / detail column widths.
constexpr int NAME_COL     = 13;
constexpr int VALUE_ID_COL = 18;
constexpr int VALUE_COL    = 10;

auto hex2(std::uint8_t value) -> std::string
{
    static constexpr std::array<char, 16> digits{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    return std::string{digits.at(value >> NIBBLE_BITS), digits.at(value & LOW_NIBBLE_MASK)};
}

// "S2" / "S0" / "-" from a node's CC list (a cheap proxy for its scheme until
// NodeSecurityStatus is tracked).
auto securityLabel(const std::vector<std::uint8_t>& ccs) -> const char*
{
    if (std::find(ccs.begin(), ccs.end(), CC_SECURITY_2) != ccs.end())
    {
        return "S2";
    }
    if (std::find(ccs.begin(), ccs.end(), CC_SECURITY_0) != ccs.end())
    {
        return "S0";
    }
    return "-";
}

// Compact "3s" / "5m" / "2h" / "4d" age from a unix-seconds timestamp.
auto renderAge(std::uint64_t updatedAt) -> std::string
{
    const auto now   = static_cast<std::int64_t>(std::time(nullptr));
    std::int64_t age = now - static_cast<std::int64_t>(updatedAt);
    if (age < 0)
    {
        age = 0;
    }
    if (age < SECONDS_PER_MINUTE)
    {
        return std::to_string(age) + "s";
    }
    if (age < SECONDS_PER_HOUR)
    {
        return std::to_string(age / SECONDS_PER_MINUTE) + "m";
    }
    if (age < SECONDS_PER_DAY)
    {
        return std::to_string(age / SECONDS_PER_HOUR) + "h";
    }
    return std::to_string(age / SECONDS_PER_DAY) + "d";
}

// Truncate `text` to `width` columns (~ if clipped). width<=0 → empty.
auto fit(const std::string& text, int width) -> std::string
{
    if (width <= 0)
    {
        return {};
    }
    if (static_cast<int>(text.size()) <= width)
    {
        return text;
    }
    if (width == 1)
    {
        return text.substr(0, 1);
    }
    return text.substr(0, static_cast<std::size_t>(width) - 1) + "~";
}

// Status bar + DaemonError banner. Returns the first body row (below the rule).
auto drawHeader() -> int
{
    int row = 0;
    {
        std::scoped_lock const lock(activity().mutex);
        const std::string dongle = activity().dongleConnected ? "* " + activity().donglePath : "o disconnected";
        mvprintw(row++, 0, " zwave-terminal   Dongle: %s   %zu nodes", dongle.c_str(), nodeModel().rows.size());

        const auto& err = activity().daemonError;
        if (!err.message.empty())
        {
            const char* label = "INFO";
            int colorPair     = 0;
            switch (err.severity)
            {
            case SEVERITY_WARN:
                label     = "WARN";
                colorPair = CP_WARN;
                break;
            case SEVERITY_ERROR:
                label     = "ERROR";
                colorPair = CP_ERROR;
                break;
            case SEVERITY_CRITICAL:
                label     = "CRITICAL";
                colorPair = CP_CRITICAL;
                break;
            default:
                break;
            }
            const bool coloured = colorPair != 0 && has_colors();
            if (coloured)
            {
                attron(COLOR_PAIR(colorPair) | A_BOLD);
            }
            mvprintw(row++,
                     0,
                     " ! %s [%s 0x%02X]: %s",
                     label,
                     err.source.c_str(),
                     static_cast<unsigned>(err.code),
                     err.message.c_str());
            if (coloured)
            {
                attroff(COLOR_PAIR(colorPair) | A_BOLD);
            }
        }

        // Pending S2 DSK confirmation (#187): prompt the operator to enter the PIN.
        const auto& dsk = activity().dskPending;
        if (dsk.active)
        {
            const bool coloured = has_colors();
            if (coloured)
            {
                attron(COLOR_PAIR(CP_WARN) | A_BOLD);
            }
            mvprintw(row++,
                     0,
                     " DSK confirm: node %u  %s  -> press [k] to enter PIN",
                     static_cast<unsigned>(dsk.nodeId),
                     dsk.dsk.c_str());
            if (coloured)
            {
                attroff(COLOR_PAIR(CP_WARN) | A_BOLD);
            }
        }
    }
    mvhline(row++, 0, '-', getmaxx(stdscr));
    return row;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): top/bottom/width are positional render bounds
auto drawNodeList(int top, int bottom, int width) -> void
{
    auto& model = nodeModel();
    int row     = top;
    mvprintw(row++, 0, " Nodes");
    if (row <= bottom)
    {
        mvprintw(row++, 0, " #   name           sec");
    }
    for (std::size_t i = 0; i < model.rows.size() && row <= bottom; ++i, ++row)
    {
        const auto& node    = model.rows.at(i);
        const bool selected = i == model.selected;
        if (selected)
        {
            attron(A_REVERSE);
        }
        const std::string name = node.name.empty() ? "(unnamed)" : node.name;
        const std::string line = " " + fit(std::to_string(static_cast<unsigned>(node.id)), 3) + "  " +
                                 fit(name, NAME_COL) + "  " + securityLabel(node.commandClasses);
        mvprintw(row, 0, "%s", fit(line, width).c_str());
        if (selected)
        {
            attroff(A_REVERSE);
        }
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): top/bottom/left/width are positional render bounds
auto drawDetail(int top, int bottom, int left, int width) -> void
{
    auto& model = nodeModel();
    int row     = top;
    if (model.selected >= model.rows.size())
    {
        mvprintw(row, left, "%s", fit("(no node selected)", width).c_str());
        return;
    }
    const auto& node = model.rows.at(model.selected);

    const std::string title =
        "Node " + std::to_string(static_cast<unsigned>(node.id)) + (node.name.empty() ? "" : " - " + node.name);
    mvprintw(row++, left, "%s", fit(title, width).c_str());
    if (row <= bottom)
    {
        const std::string classes = "class " + hex2(node.basicType) + "/" + hex2(node.genericType) + "/" +
                                    hex2(node.specificType) + "  sec " + securityLabel(node.commandClasses);
        mvprintw(row++, left, "%s", fit(classes, width).c_str());
    }
    if (row <= bottom)
    {
        mvprintw(row++, left, "%s", fit("CC " + formatCcList(node.commandClasses), width).c_str());
    }
    if (row <= bottom)
    {
        mvprintw(row++, left, "%s", fit("values:", width).c_str());
    }
    if (node.values.empty() && row <= bottom)
    {
        mvprintw(row++, left, "%s", fit("  (none reported yet)", width).c_str());
    }
    for (const auto& value : node.values)
    {
        if (row > bottom)
        {
            break;
        }
        const std::string line = "  " + fit(value.valueId, VALUE_ID_COL) + " " + fit(value.value, VALUE_COL) + " (" +
                                 renderAge(value.updatedAt) + ")";
        mvprintw(row++, left, "%s", fit(line, width).c_str());
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): top/bottom are positional render bounds
auto drawLog(int top, int bottom) -> void
{
    mvhline(top, 0, '-', getmaxx(stdscr));
    int row = top + 1;
    std::scoped_lock const lock(activity().mutex);
    const auto& log         = activity().log;
    const int available     = bottom - row + 1;
    const std::size_t start = (available > 0 && log.size() > static_cast<std::size_t>(available))
                                  ? log.size() - static_cast<std::size_t>(available)
                                  : 0;
    for (std::size_t idx = start; idx < log.size() && row <= bottom; ++idx, ++row)
    {
        mvprintw(row, 0, "%s", fit(log.at(idx), getmaxx(stdscr)).c_str());
    }
}
}  // namespace

auto runActionMenu(const char* title, const std::vector<MenuItem>& items) -> void
{
    erase();
    int row = 0;
    mvprintw(row++, 0, " %s  -  press a key (any other to cancel)", title);
    mvhline(row++, 0, '-', getmaxx(stdscr));
    for (const auto& item : items)
    {
        mvprintw(row++, 0, "  [%c] %s", item.key, item.label.c_str());
    }
    refresh();

    timeout(-1);  // blocking
    const int key = getch();
    timeout(UI_REFRESH_MS);

    for (const auto& item : items)
    {
        if (item.key == key)
        {
            item.action();
            return;
        }
    }
}

auto runInfoModal(const std::string& title, const std::vector<std::string>& lines) -> void
{
    erase();
    const int maxX = getmaxx(stdscr);
    const int maxY = getmaxy(stdscr);
    int row        = 0;
    mvprintw(row++, 0, " %s", fit(title, maxX - 1).c_str());
    mvhline(row++, 0, '-', maxX);
    for (const auto& line : lines)
    {
        if (row >= maxY - 1)
        {
            break;
        }
        mvprintw(row++, 0, " %s", fit(line, maxX - 1).c_str());
    }
    mvprintw(maxY - 1, 0, "%s", fit(" press any key to return", maxX).c_str());
    refresh();

    timeout(-1);  // blocking
    getch();
    timeout(UI_REFRESH_MS);
}

auto draw(std::uint8_t lastSession) -> void
{
    erase();
    const int maxY = getmaxy(stdscr);
    const int maxX = getmaxx(stdscr);

    const int bodyTop    = drawHeader();
    const int hintRow    = maxY - 1;
    const int logBottom  = hintRow - 1;
    const int logTop     = logBottom - LOG_ROWS;  // includes the log's divider row
    const int bodyBottom = logTop - 1;
    const int listWidth  = std::min(LIST_WIDTH_MAX, maxX / 2);
    const int detailLeft = listWidth + 2;

    if (bodyBottom >= bodyTop)
    {
        drawNodeList(bodyTop, bodyBottom, listWidth);
        for (int row = bodyTop; row <= bodyBottom; ++row)
        {
            mvaddch(row, listWidth, '|');
        }
        drawDetail(bodyTop, bodyBottom, detailLeft, maxX - detailLeft);
    }

    if (logTop >= bodyTop && logTop < hintRow)
    {
        drawLog(logTop, logBottom);
    }

    // Scope-grouped key hints (#215): contextual verbs act on the selected node;
    // network verbs are inclusion/exclusion; the rest are menus / global.
    const std::string hint = " up/dn select | [c]ontrol [g]et [f]ail [L]ifeline | [a]dd [x]remove |"
                             " [p]olicy [e]scenes [h]vac [l]ist [n]et [i]nfo [r]efresh (s:" +
                             std::to_string(static_cast<unsigned>(lastSession)) + ") [q]uit";
    mvprintw(hintRow, 0, "%s", fit(hint, maxX).c_str());
    refresh();
}
}  // namespace zwt
