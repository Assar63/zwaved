#ifndef ZWAVED_S2_MPAN_HPP
#define ZWAVED_S2_MPAN_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"         // S2::Crypto::Key, Block
#include "Encapsulation.hpp"  // S2::Encapsulation::CcmNonce
// IWYU pragma: end_exports

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

/// Security S2 (CC 0x9F) **Multicast Pre-Agreed Nonce** — phase 10 (#188),
/// SDS13783 §4.2.6.4.22-23 and §4.2.6.5.3-7, §4.2.6.5.14-15.
///
/// Where a SPAN (Span.hpp) is a *pairwise* nonce generator both peers advance
/// in lockstep, an MPAN is a *group* nonce: one sender distributes the 16-byte
/// inner state to every member of a multicast group, and each member advances
/// its own copy identically. That lets one encrypted broadcast be decrypted by
/// the whole group.
///
/// The generator is deliberately simpler than the SPAN's CTR_DRBG — it is a
/// plain AES-128-ECB counter (§4.2.6.4.23 NextMPAN):
///   1. encrypt the 16-byte inner state under KeyMPAN,
///   2. take the 13 most significant bytes as the CCM nonce,
///   3. increment the inner state as a big-endian 128-bit integer.
///
/// Two extension objects carry the group plumbing on the wire:
///   - **MPAN Extension** (§4.2.6.5.14) pushes the full inner state to a
///     receiver. It is **encrypted**, and rides a singlecast frame.
///   - **MGRP Extension** (§4.2.6.5.15) tags a multicast or singlecast
///     follow-up frame with its Group ID so the receiver knows which MPAN to
///     use. It is **unencrypted**, and must never share a frame with an MPAN
///     Extension.
///
/// Pure state machine + codec, in the same spirit as Span / NonceSync: no
/// threads, no bus, no I/O.
///
/// **Not yet wired to a send path.** The daemon has no multicast SendData
/// (FUNC_ID_ZW_SEND_DATA_MULTI 0x14, issue #4), so nothing calls this in
/// anger yet — #188's second checkbox is blocked on #4. This is the pure
/// half, landed the way every other S2 phase was.
namespace S2::MPAN
{
/// The MPAN inner state: a 16-byte counter (§4.2.6.4.22).
using InnerState = Crypto::Block;

// ---- Extension objects ----------------------------------------------
// Header byte layout is shared with the SPAN extension (see NonceSync.hpp):
// bits0-5 type, bit6 critical, bit7 more-to-follow.

constexpr std::uint8_t MPAN_EXTENSION_TYPE   = 0x02;
constexpr std::uint8_t MPAN_EXTENSION_LENGTH = 19;    // 1 len + 1 flags/type + 1 groupId + 16 state
constexpr std::uint8_t MPAN_EXTENSION_FLAGS  = 0x42;  // moreToFollow=0, critical=1, type=MPAN(2)

constexpr std::uint8_t MGRP_EXTENSION_TYPE   = 0x03;
constexpr std::uint8_t MGRP_EXTENSION_LENGTH = 3;     // 1 len + 1 flags/type + 1 groupId
constexpr std::uint8_t MGRP_EXTENSION_FLAGS  = 0x43;  // moreToFollow=0, critical=1, type=MGRP(3)

/// One multicast group's nonce generator. Both the sender and every receiver
/// hold an identical copy and advance it in step.
class Generator
{
  public:
    explicit Generator(const InnerState& initial)
        : state_(initial)
    {
    }

    /// NextMPAN (§4.2.6.4.23): the 13-byte CCM nonce for the next multicast
    /// frame, advancing the inner state. Sender and receiver must call this
    /// exactly once per frame or they fall out of step.
    [[nodiscard]] auto nextNonce(const Crypto::Key& keyMpan) -> Encapsulation::CcmNonce;

    /// The current inner state — what an MPAN Extension distributes, and what
    /// gets persisted.
    [[nodiscard]] auto innerState() const -> const InnerState&
    {
        return state_;
    }

    /// Advance without producing a nonce. §4.2.6.5.15: a receiver increments
    /// the matching MPAN after decrypting a frame carrying an MGRP extension.
    auto advance() -> void;

    friend auto operator==(const Generator&, const Generator&) -> bool = default;

  private:
    InnerState state_{};
};

/// Per-entry lifecycle from the MPAN table (§4.2.6.5.4, Table 4.11).
enum class EntryState : std::uint8_t
{
    Free,  ///< not in use
    Used,  ///< in use and believed synchronised
    Mos,   ///< out of sync; owes a Nonce Report with the MOS flag
};

/// One row of the MPAN table (§4.2.6.5.4).
struct Entry
{
    std::uint8_t owner   = 0;  ///< NodeID distributing this MPAN (receive side)
    std::uint8_t groupId = 0;  ///< unique per owner
    Generator generator{InnerState{}};
    EntryState entryState = EntryState::Free;
};

/// The MPAN table: entries keyed by the `(owner NodeID, Group ID)` pair a
/// multicast group is identified by (§4.2.6.5.3).
///
/// Holds both the groups this node *sends* to (owner == our own node id) and
/// the ones it *receives* — the spec keeps them in one table, and the owner
/// field is what tells them apart.
class Table
{
  public:
    using Key = std::pair<std::uint8_t, std::uint8_t>;  // (owner, groupId)

    /// Install or replace a group's inner state, marking it Used. This is what
    /// an inbound MPAN Extension does on the receive side, and what the sender
    /// does when it creates a group.
    auto set(std::uint8_t owner, std::uint8_t groupId, const InnerState& state) -> void;

    /// The generator for a group, or nullptr unless the entry is `Used` — an
    /// out-of-sync (`Mos`) entry has no state anyone may encrypt or decrypt
    /// with, so it is deliberately not handed back. Non-const so the caller can
    /// draw a nonce (which advances it).
    [[nodiscard]] auto find(std::uint8_t owner, std::uint8_t groupId) -> Generator*;

    /// Whether the group has a *usable* MPAN (`Used`). False for both an
    /// unknown group and a known-but-out-of-sync one; `entryState` tells them
    /// apart.
    [[nodiscard]] auto contains(std::uint8_t owner, std::uint8_t groupId) const -> bool;

    /// Mark a group out of sync — set when a frame arrives for a Group ID we
    /// have no MPAN for, so the next singlecast can answer with the MOS flag.
    auto markOutOfSync(std::uint8_t owner, std::uint8_t groupId) -> void;

    [[nodiscard]] auto entryState(std::uint8_t owner, std::uint8_t groupId) const -> EntryState;

    /// Forget a group (§4.2.6.5.6 — a removed member drops the group once it
    /// stops seeing the MPAN extension).
    auto remove(std::uint8_t owner, std::uint8_t groupId) -> void;

    [[nodiscard]] auto size() const -> std::size_t
    {
        return entries_.size();
    }

    /// Every entry, for persistence and for tests.
    [[nodiscard]] auto entries() const -> const std::map<Key, Entry>&
    {
        return entries_;
    }

  private:
    std::map<Key, Entry> entries_;
};

// ---- Wire codecs -----------------------------------------------------

/// MPAN Extension (§4.2.6.5.14): `[19][0x42][groupId][inner·16]`.
/// **Encrypted** — belongs in the encrypted extension chain.
[[nodiscard]] auto encodeMpanExtension(std::uint8_t groupId, const InnerState& state) -> std::vector<std::uint8_t>;

/// Scan a chain of *encrypted* extension objects for an MPAN Extension.
/// std::nullopt if absent or malformed.
[[nodiscard]] auto findMpanExtension(std::span<const std::uint8_t> extensions)
    -> std::optional<std::pair<std::uint8_t, InnerState>>;

/// MGRP Extension (§4.2.6.5.15): `[3][0x43][groupId]`.
/// **Unencrypted** — and must never be sent alongside an MPAN Extension.
[[nodiscard]] auto encodeMgrpExtension(std::uint8_t groupId) -> std::vector<std::uint8_t>;

/// Scan a chain of *unencrypted* extension objects for an MGRP Extension and
/// return its Group ID. std::nullopt if absent or malformed.
[[nodiscard]] auto findMgrpExtension(std::span<const std::uint8_t> extensions) -> std::optional<std::uint8_t>;
}  // namespace S2::MPAN

#endif  // ZWAVED_S2_MPAN_HPP
