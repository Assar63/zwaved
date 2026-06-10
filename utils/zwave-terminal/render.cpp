#include "render.hpp"

#include "activity.hpp"
#include "constants.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <ncurses.h>

namespace zwt
{
auto runActionMenu(const char* title, const std::vector<MenuItem>& items) -> void
{
    erase();
    int row = 0;
    mvprintw(row++, 0, " %s  —  press a key (any other to cancel)", title);
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

auto draw(std::uint8_t lastSession) -> void
{
    erase();
    int row = 0;

    {
        std::scoped_lock const lock(activity().mutex);
        const std::string status =
            activity().dongleConnected ? "connected (" + activity().donglePath + ")" : "disconnected";
        mvprintw(row++, 0, " zwave-terminal  -  Dongle: %s", status.c_str());

        // DaemonError banner — only shown while there's a current error
        // (empty message == recovered). Colour-coded by severity.
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
                     " ! %s [%s code=0x%02X]: %s",
                     label,
                     err.source.c_str(),
                     static_cast<unsigned>(err.code),
                     err.message.c_str());
            if (coloured)
            {
                attroff(COLOR_PAIR(colorPair) | A_BOLD);
            }
        }
    }
    mvhline(row++, 0, '-', getmaxx(stdscr));

    mvprintw(row++, 0, "  [1] Add zwave node          [2] Remove zwave node");
    mvprintw(row++, 0, "  [g] Get from node…          [c] Control / set on node…");
    mvprintw(row++, 0, "  [p] Policy…                 [e] Scenes…           [h] Logical thermostat…");
    mvprintw(row++, 0, "  [l] List included nodes     [n] Network status     [i] Dongle info");
    mvprintw(row++, 0, "  [f] Remove failed node      [L] Set lifeline (controller -> group 1)");
    mvprintw(row++, 0, "  [s] Stop current operation (session %u)", static_cast<unsigned>(lastSession));
    mvprintw(row++, 0, "  [q] Quit");
    mvhline(row++, 0, '-', getmaxx(stdscr));

    {
        std::scoped_lock const lock(activity().mutex);
        const auto& log              = activity().log;
        const int available          = getmaxy(stdscr) - row;
        const std::size_t startIndex = (available > 0 && log.size() > static_cast<std::size_t>(available))
                                           ? log.size() - static_cast<std::size_t>(available)
                                           : 0;
        for (std::size_t idx = startIndex; idx < log.size() && row < getmaxy(stdscr); ++idx)
        {
            mvprintw(row++, 0, "%s", log.at(idx).c_str());
        }
    }
    refresh();
}
}  // namespace zwt
