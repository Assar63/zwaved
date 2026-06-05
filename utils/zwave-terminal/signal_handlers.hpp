#ifndef ZWAVE_TERMINAL_SIGNAL_HANDLERS_HPP
#define ZWAVE_TERMINAL_SIGNAL_HANDLERS_HPP

// sdbus-c++ Message.h uses std::copy_n without including <algorithm>; pull it
// in first so any translation unit that includes this header compiles.
#include <algorithm>  // IWYU pragma: keep

#include <sdbus-c++/IProxy.h>

// D-Bus signal subscriptions for the zwave-terminal client: every typed
// report / status signal from the daemon, rendered into the activity log.
// See #111.
namespace zwt
{
auto registerSignalHandlers(sdbus::IProxy& proxy) -> void;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_SIGNAL_HANDLERS_HPP
