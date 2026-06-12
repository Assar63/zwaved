#ifndef ZWAVED_S0_NONCE_TABLE_HPP
#define ZWAVED_S0_NONCE_TABLE_HPP

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>

/// Security S0 (CC 0x98) nonce protocol — phase 3 of the S0 epic (#26 / #164).
namespace S0
{
constexpr std::size_t NONCE_SIZE = 8;
using Nonce                      = std::array<std::uint8_t, NONCE_SIZE>;

/// Per-peer pool of the single-use nonces this controller has issued (in reply
/// to an inbound SECURITY_NONCE_GET). Each nonce is identified by its first
/// byte; an inbound SECURITY_MESSAGE_ENCAPSULATION frame echoes that id so the
/// receiver can recover the matching nonce to build the decryption IV. Nonces
/// expire after a freshness window (S0 mandates ~10 s) and are consumed on use.
///
/// The clock is injected per call (`now`) so the freshness logic is
/// deterministically testable; production callers pass `Clock::now()`.
class NonceTable
{
  public:
    using Clock                                        = std::chrono::steady_clock;
    static constexpr Clock::duration DEFAULT_FRESHNESS = std::chrono::seconds(10);

    explicit NonceTable(Clock::duration freshness = DEFAULT_FRESHNESS);

    /// Generate (via libcrypto RAND_bytes), store, and return a fresh nonce for
    /// `peer` stamped `now`. A new nonce whose id collides with a live one for
    /// the same peer replaces it.
    [[nodiscard]] auto generate(std::uint8_t peer, Clock::time_point now) -> Nonce;

    /// Consume the still-fresh nonce for (`peer`, `nonceId`), or std::nullopt
    /// if there is none or it has expired. Single-use: a hit is removed.
    [[nodiscard]] auto take(std::uint8_t peer, std::uint8_t nonceId, Clock::time_point now) -> std::optional<Nonce>;

    /// Drop every nonce older than the freshness window as of `now`.
    auto purgeExpired(Clock::time_point now) -> void;

    /// Number of stored entries (live or not-yet-purged) — for tests.
    [[nodiscard]] auto size() const -> std::size_t;

  private:
    struct Entry
    {
        Nonce nonce{};
        Clock::time_point issued;  // value-initialized by the clock's default ctor
    };

    Clock::duration freshness_;
    std::map<std::pair<std::uint8_t, std::uint8_t>, Entry> entries_;  // key: (peer, nonceId)
};

/// Process-wide table of nonces this controller has issued — shared between the
/// inbound NONCE_GET responder (#164) and the inbound decryptor (#165).
[[nodiscard]] auto issuedNonces() -> NonceTable&;
}  // namespace S0

#endif  // ZWAVED_S0_NONCE_TABLE_HPP
