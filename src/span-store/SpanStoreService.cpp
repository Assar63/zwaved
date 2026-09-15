// Constructor-armed wiring that makes the persisted SPAN table live (#199) —
// the thin companion to the pure store in SpanStore.cpp, mirroring
// NetworkKeyService's relationship to NetworkKey.
//
// A SPAN is an AES-CTR_DRBG that both peers advance in lockstep. If the daemon
// restarts and forgets its half, the next encrypted frame fails to decrypt and
// costs a Nonce-Sync (SOS) round-trip per peer to re-establish. Persisting the
// 32-byte inner state across a restart removes that.
//
// **Save policy: checkpoint + shutdown, not save-on-advance.** A SPAN advances
// on every encrypt() and every receiveNonce(), so writing on each advance would
// put a SQLite write in the S2 hot path — one per encrypted frame, on flash, on
// an embedded target. Instead a checkpoint thread snapshots every CHECKPOINT
// seconds and writes only the peers whose inner state actually changed since
// the last write, and a final save runs at shutdown.
//
// The cost is a bounded resync window: SPANs advanced since the last checkpoint
// are lost on an *unclean* exit (power cut, SIGKILL), and those peers pay one
// SOS round-trip on the next frame — exactly what happens on every restart
// today, so this is strictly better. A clean shutdown loses nothing.
//
// Teardown: the daemon's __attribute__((destructor)) functions run from
// .fini_array, *after* the C runtime has destroyed __cxa_atexit-registered
// statics — so, as everywhere else in this codebase, the state struct joins its
// own thread in its own destructor rather than relying on a destructor
// function. Priority CONFIG_SECURITY_PRIO (111) is deliberate: constructors run
// lowest-first, so this comes up before the orchestrators (204) and has the
// table loaded before they can use it; destructors run in reverse, so this one
// tears down *after* the orchestrators have stopped mutating SPANs, and the
// shutdown save sees a quiescent table.

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"
#include "../zwave-protocol/security/s2/Transport.hpp"
#include "../zwaved.h"  // IWYU pragma: keep — CONFIG_SECURITY_PRIO
#include "SpanStore.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/prctl.h>

namespace
{
/// How often the checkpoint thread snapshots advanced SPANs.
constexpr auto CHECKPOINT = std::chrono::seconds(60);

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct State
{
    MessageBus::SubscriptionGuard dongleSub;
    MessageBus::SubscriptionGuard checkpointSub;
    std::mutex mutex;
    std::condition_variable wakeUp;
    bool running   = false;
    bool homeBound = false;
    std::thread thread;

    /// What we last wrote per peer, so a checkpoint skips unchanged SPANs
    /// rather than rewriting the whole table every minute.
    std::map<std::uint8_t, S2::SPAN::InnerState> lastWritten;

    State()                                    = default;
    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
    ~State();
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> State&
{
    static State instance;
    return instance;
}

/// Write every SPAN that has advanced since the last checkpoint; returns how
/// many rows were written.
///
/// **Must run with the live SPAN table quiescent** — i.e. inside a bus handler
/// (so the recursive bus mutex serialises it against the S2 orchestrators), or
/// during teardown once those orchestrators are gone. `SpanManager` has no
/// internal locking of its own.
///
/// Takes its state by reference rather than calling state(): the shutdown save
/// runs from ~State, and re-entering the accessor for an object already being
/// destroyed is exactly the kind of subtlety worth not having.
auto checkpoint(State& self) -> std::size_t
{
    const std::scoped_lock lock(self.mutex);
    if (!self.homeBound)
    {
        return 0;  // no home yet — nothing is scoped, so nothing to persist
    }

    std::size_t written = 0;
    for (const auto& [peer, inner] : S2::Transport::manager().exportAll())
    {
        const auto previous = self.lastWritten.find(peer);
        if (previous != self.lastWritten.end() && previous->second == inner)
        {
            continue;  // unchanged since the last write
        }
        SpanStore::instance().save(peer, inner);
        self.lastWritten[peer] = inner;
        ++written;
    }
    return written;
}

auto checkpointLoop() -> void
{
    prctl(PR_SET_NAME, "ZWaveSpanCk", 0, 0, 0);  // NOLINT(misc-include-cleaner): PR_SET_NAME from <sys/prctl.h>
    while (true)
    {
        {
            std::unique_lock lock(state().mutex);
            state().wakeUp.wait_for(lock, CHECKPOINT, [] { return !state().running; });
            if (!state().running)
            {
                return;  // the shutdown save runs in ~State, on the main thread
            }
        }
        // Publish rather than checkpoint here: publish() holds the recursive
        // bus mutex across dispatch, so the handler touches the shared SPAN
        // table under the same lock as the S2 orchestrators. Doing the work on
        // this thread would race them.
        MessageBus::publish(MessageBus::S2SpanCheckpointDue{});
    }
}

/// Bind the store to this network and restore its SPANs into the live
/// transport table, so traffic resumes in lockstep instead of forcing an SOS.
auto bindAndRestore(const std::vector<std::uint8_t>& homeId) -> void
{
    SpanStore::instance().setHomeId(homeId);
    const auto persisted = SpanStore::instance().loadAll();
    for (const auto& [peer, inner] : persisted)
    {
        S2::Transport::manager().importSpan(peer, inner);
    }
    {
        const std::scoped_lock lock(state().mutex);
        state().homeBound   = true;
        state().lastWritten = persisted;  // freshly loaded == freshly written
    }
    if (!persisted.empty())
    {
        Logger::info("[span-store] restored " + std::to_string(persisted.size()) +
                     " SPAN(s) — S2 peers resume without a Nonce-Sync resync");
    }
}

State::~State()
{
    {
        const std::scoped_lock lock(mutex);
        if (!running)
        {
            return;
        }
        running = false;
    }
    wakeUp.notify_all();
    if (thread.joinable())
    {
        thread.join();
    }
    // Final save. Safe to run directly rather than through the bus: this
    // destructor runs at priority 111, by which point the orchestrators (204)
    // and the protocol / external-api threads (201-203) are already torn down,
    // so nothing else can be touching a SPAN.
    if (const auto written = checkpoint(*this); written > 0)
    {
        Logger::info("[span-store] saved " + std::to_string(written) + " SPAN(s) at shutdown");
    }
}

__attribute__((constructor(CONFIG_SECURITY_PRIO))) auto startSpanStoreService() -> void
{
    // Touch the store singleton *before* this TU's own state(), so its
    // function-local static is registered with __cxa_atexit first and is
    // therefore destroyed last. ~State() runs the shutdown save, which needs a
    // live store; the reverse order would have it writing into a destroyed one.
    // (Same reasoning as MessageBus::touch() in the Logger constructor.)
    static_cast<void>(SpanStore::instance());

    // DongleInfo is retained but not published until the dongle comes up
    // (priority 201), so this subscribe usually fires later, on the protocol
    // thread — that is where the home ID, and therefore the row scope, is known.
    state().dongleSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::DongleInfo>(
        [](const MessageBus::DongleInfo& info) -> void { bindAndRestore(info.homeId); }));

    state().checkpointSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::S2SpanCheckpointDue>(
        [](const MessageBus::S2SpanCheckpointDue&) -> void
        {
            if (const auto written = checkpoint(state()); written > 0)
            {
                Logger::debug("[span-store] checkpointed " + std::to_string(written) + " advanced SPAN(s)");
            }
        }));

    const std::scoped_lock lock(state().mutex);
    state().running = true;
    state().thread  = std::thread(checkpointLoop);
}
}  // namespace
