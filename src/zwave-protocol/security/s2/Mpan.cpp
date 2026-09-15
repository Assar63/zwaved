#include "Mpan.hpp"

#include "Crypto.hpp"
#include "Encapsulation.hpp"
#include "NonceSync.hpp"  // the shared extension-header bit layout
#include "Span.hpp"       // SPAN::incrementCounter — the same big-endian counter rule

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
/// Walk a chain of concatenated extension objects, handing each
/// `(flags, body)` to `visit` until it returns true. Shared by the MPAN and
/// MGRP scanners; mirrors NonceSync::findSpanExtension's tolerance — a
/// malformed chain simply yields no match rather than throwing.
template <typename Visitor> auto forEachExtension(std::span<const std::uint8_t> extensions, Visitor visit) -> bool
{
    std::size_t offset = 0;
    while (offset + 2 <= extensions.size())
    {
        const std::size_t length = extensions[offset];
        if (length < 2 || offset + length > extensions.size())
        {
            return false;  // malformed — stop rather than guess
        }
        const std::uint8_t flags = extensions[offset + 1];
        if (visit(flags, extensions.subspan(offset + 2, length - 2)))
        {
            return true;
        }
        if ((flags & S2::NonceSync::EXT_MORE_FOLLOW_BIT) == 0)
        {
            return false;  // that was the last object in the chain
        }
        offset += length;
    }
    return false;
}
}  // namespace

auto S2::MPAN::Generator::nextNonce(const Crypto::Key& keyMpan) -> Encapsulation::CcmNonce
{
    // §4.2.6.4.23 NextMPAN: encrypt the inner state, take the 13 most
    // significant bytes, then increment the state.
    const auto block = Crypto::aesEcbEncrypt(keyMpan, state_);

    Encapsulation::CcmNonce nonce{};
    std::copy_n(block.begin(), nonce.size(), nonce.begin());

    SPAN::incrementCounter(state_);
    return nonce;
}

auto S2::MPAN::Generator::advance() -> void
{
    SPAN::incrementCounter(state_);
}

// ---- Table -----------------------------------------------------------

auto S2::MPAN::Table::set(std::uint8_t owner, std::uint8_t groupId, const InnerState& state) -> void
{
    entries_.insert_or_assign(
        Key{owner, groupId},
        Entry{.owner = owner, .groupId = groupId, .generator = Generator{state}, .entryState = EntryState::Used});
}

auto S2::MPAN::Table::find(std::uint8_t owner, std::uint8_t groupId) -> Generator*
{
    const auto found = entries_.find(Key{owner, groupId});
    if (found == entries_.end() || found->second.entryState != EntryState::Used)
    {
        // Free means "never had one"; Mos means "had one, it is stale" — in
        // both cases there is no state anyone may encrypt or decrypt with.
        return nullptr;
    }
    return &found->second.generator;
}

auto S2::MPAN::Table::contains(std::uint8_t owner, std::uint8_t groupId) const -> bool
{
    const auto found = entries_.find(Key{owner, groupId});
    return found != entries_.end() && found->second.entryState == EntryState::Used;
}

auto S2::MPAN::Table::markOutOfSync(std::uint8_t owner, std::uint8_t groupId) -> void
{
    // A group we've never seen still needs a row: it is precisely the "I have
    // no MPAN for this Group ID" case that owes a MOS Nonce Report.
    auto& entry      = entries_[Key{owner, groupId}];
    entry.owner      = owner;
    entry.groupId    = groupId;
    entry.entryState = EntryState::Mos;
}

auto S2::MPAN::Table::entryState(std::uint8_t owner, std::uint8_t groupId) const -> EntryState
{
    const auto found = entries_.find(Key{owner, groupId});
    return found == entries_.end() ? EntryState::Free : found->second.entryState;
}

auto S2::MPAN::Table::remove(std::uint8_t owner, std::uint8_t groupId) -> void
{
    entries_.erase(Key{owner, groupId});
}

// ---- Wire codecs -----------------------------------------------------

auto S2::MPAN::encodeMpanExtension(std::uint8_t groupId, const InnerState& state) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    out.reserve(MPAN_EXTENSION_LENGTH);
    out.push_back(MPAN_EXTENSION_LENGTH);
    out.push_back(MPAN_EXTENSION_FLAGS);
    out.push_back(groupId);
    out.insert(out.end(), state.begin(), state.end());
    return out;
}

auto S2::MPAN::findMpanExtension(std::span<const std::uint8_t> extensions)
    -> std::optional<std::pair<std::uint8_t, InnerState>>
{
    std::optional<std::pair<std::uint8_t, InnerState>> result;
    forEachExtension(extensions,
                     [&result](std::uint8_t flags, std::span<const std::uint8_t> body) -> bool
                     {
                         if ((flags & NonceSync::EXT_TYPE_MASK) != MPAN_EXTENSION_TYPE)
                         {
                             return false;
                         }
                         if (body.size() != 1 + std::tuple_size_v<InnerState>)
                         {
                             return false;  // wrong size for an MPAN extension — not a match
                         }
                         InnerState state{};
                         std::copy_n(body.begin() + 1, state.size(), state.begin());
                         result.emplace(body[0], state);
                         return true;
                     });
    return result;
}

auto S2::MPAN::encodeMgrpExtension(std::uint8_t groupId) -> std::vector<std::uint8_t>
{
    return {MGRP_EXTENSION_LENGTH, MGRP_EXTENSION_FLAGS, groupId};
}

auto S2::MPAN::findMgrpExtension(std::span<const std::uint8_t> extensions) -> std::optional<std::uint8_t>
{
    std::optional<std::uint8_t> result;
    forEachExtension(extensions,
                     [&result](std::uint8_t flags, std::span<const std::uint8_t> body) -> bool
                     {
                         if ((flags & NonceSync::EXT_TYPE_MASK) != MGRP_EXTENSION_TYPE || body.size() != 1)
                         {
                             return false;
                         }
                         result = body[0];
                         return true;
                     });
    return result;
}
