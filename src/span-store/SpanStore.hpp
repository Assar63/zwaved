#ifndef ZWAVED_SPAN_STORE_HPP
#define ZWAVED_SPAN_STORE_HPP

// IWYU pragma: begin_exports
#include "../zwave-protocol/security/s2/Span.hpp"  // S2::SPAN::InnerState
// IWYU pragma: end_exports

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

/// Durable per-peer Security S2 SPAN state (#199). A SPAN is an AES-CTR_DRBG
/// both peers advance in lockstep; if the daemon restarts and forgets its half,
/// the next encrypted frame fails to decrypt and a Nonce-Sync (SOS) round-trip
/// is needed to re-establish. Persisting the inner state (Key‖V, 32 bytes) lets
/// a restart resume in lockstep and skip that resync.
///
/// SQLite, sharing `nodes.db` with NodeRegistry on its own connection (the file
/// is safe for multiple in-process connections). Follows PendingQueue's split: a
/// testable `Store` class (two instances against one file model a daemon
/// restart) plus a `SpanStore::instance()` singleton wired via StorageConfig.
namespace SpanStore
{
/// One peer's persisted SPAN: the node id + its 32-byte inner state.
struct Entry
{
    std::uint8_t peer = 0;
    S2::SPAN::InnerState state{};
};

/// One instance owns one sqlite3 connection to one file. Move-only.
class Store
{
  public:
    /// Open / create the SQLite file at `dbPath`, creating the `span_state`
    /// table if absent (tolerant of co-existing tables owned by other modules).
    explicit Store(const std::filesystem::path& dbPath);
    ~Store();

    Store(const Store&)                        = delete;
    auto operator=(const Store&) -> Store&     = delete;
    Store(Store&&) noexcept                    = default;
    auto operator=(Store&&) noexcept -> Store& = default;

    /// Bind to a Z-Wave network's 4-byte home ID. All save / remove / load calls
    /// scope to this home; rows for other homes stay in the table, out of view.
    auto setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void;

    /// Persist (insert or replace) `peer`'s SPAN inner state. No-op (warns) if no
    /// home ID is bound.
    auto save(std::uint8_t peer, const S2::SPAN::InnerState& state) -> void;

    /// Drop `peer`'s persisted SPAN (e.g. on a forced resync).
    auto remove(std::uint8_t peer) -> void;

    /// Every persisted SPAN for the bound home, keyed by peer node id.
    [[nodiscard]] auto loadAll() -> std::map<std::uint8_t, S2::SPAN::InnerState>;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
}  // namespace SpanStore

#endif  // ZWAVED_SPAN_STORE_HPP
