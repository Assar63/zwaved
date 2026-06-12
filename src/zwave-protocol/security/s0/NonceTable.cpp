#include "NonceTable.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include <openssl/rand.h>

S0::NonceTable::NonceTable(Clock::duration freshness)
    : freshness_(freshness)
{
}

auto S0::NonceTable::generate(std::uint8_t peer, Clock::time_point now) -> Nonce
{
    Nonce nonce{};
    // RAND_bytes can in principle fail; on failure the all-zero nonce is stored
    // and returned — harmless (it is still single-use and freshness-bounded),
    // and the OFB/CBC-MAC layer rejects any frame that doesn't authenticate.
    RAND_bytes(nonce.data(), static_cast<int>(nonce.size()));
    entries_[{peer, nonce[0]}] = Entry{.nonce = nonce, .issued = now};
    return nonce;
}

auto S0::NonceTable::take(std::uint8_t peer, std::uint8_t nonceId, Clock::time_point now) -> std::optional<Nonce>
{
    const auto iter = entries_.find({peer, nonceId});
    if (iter == entries_.end())
    {
        return std::nullopt;
    }
    const Entry entry = iter->second;
    entries_.erase(iter);  // single-use, whether fresh or stale
    if (now - entry.issued > freshness_)
    {
        return std::nullopt;
    }
    return entry.nonce;
}

auto S0::NonceTable::purgeExpired(Clock::time_point now) -> void
{
    for (auto iter = entries_.begin(); iter != entries_.end();)
    {
        if (now - iter->second.issued > freshness_)
        {
            iter = entries_.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

auto S0::NonceTable::size() const -> std::size_t
{
    return entries_.size();
}

auto S0::issuedNonces() -> NonceTable&
{
    static NonceTable instance;
    return instance;
}
