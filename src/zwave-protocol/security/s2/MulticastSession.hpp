#ifndef ZWAVED_S2_MULTICAST_SESSION_HPP
#define ZWAVED_S2_MULTICAST_SESSION_HPP

// IWYU pragma: begin_exports
#include "Crypto.hpp"         // S2::Crypto::Key
#include "Encapsulation.hpp"  // S2::Encapsulation::Context
#include "Mpan.hpp"           // S2::MPAN::Generator, InnerState
// IWYU pragma: end_exports

#include <array>
#include <cstdint>
#include <span>
#include <vector>

/// Security S2 (CC 0x9F) **multicast send sequencing** — #188's wiring half,
/// SDS13783 §4.2.6.5.3-7.
///
/// An S2 multicast round is three frame kinds, not one (§4.2.6.5.3, worked in
/// Figure 4.18):
///
///   S2 MC    the multicast itself — a broadcast MESSAGE_ENCAPSULATION carrying
///            an **unencrypted MGRP extension** (the Group ID) and encrypted
///            under the group's **MPAN** nonce. Its AAD Destination Tag is the
///            Group ID, not a node id (§4.2.6.4.6) — which is why the existing
///            Encapsulation codec expresses a multicast frame unchanged.
///   S2 SC-F  a singlecast follow-up to each member, encrypted under that
///            member's ordinary **SPAN**, also carrying the MGRP extension. It
///            confirms delivery and closes the replay window a bare multicast
///            would leave open.
///   MPAN push  when a member reports MOS ("I have no MPAN for that group"),
///            a singlecast carrying an **encrypted MPAN extension** with the
///            current inner state resynchronises it.
///
/// This class owns the group's MPAN and builds the MC frame and the extension
/// objects; it deliberately does **not** own SPANs, group membership, or any
/// I/O. The caller pairs `followUpExtension()` with its own SpanManager to
/// produce each SC-F, which keeps this pure and testable — the same split as
/// Span / NonceSync.
///
/// **MPAN advance: +2 per acknowledged round** (one nonce consumed building the
/// MC frame, plus one more). This follows the worked example in Figure 4.17,
/// where the sender's state runs #N → #N+2 → #N+4 → #N+6 across successive
/// multicast rounds.
///
/// Note the spec is **self-contradictory here**: Table 4.9 says an acknowledged
/// multicast advances the sender by 3, while Figure 4.17 shows 2. A fixed 3 also
/// sits oddly with a follow-up count that varies with group size. We follow the
/// worked example on the grounds that it is concrete, and `ADVANCE_PER_ROUND`
/// isolates the choice to one constant — if on-bench acceptance (#189) shows
/// real devices expecting 3, that is the single line to change. Getting it wrong
/// desynchronises every group member silently, so this is the first thing to
/// check if multicast decrypts fail on hardware.
namespace S2::Multicast
{
/// Total MPAN inner-state advance per acknowledged multicast round (Figure 4.17).
constexpr unsigned ADVANCE_PER_ROUND = 2;

/// One multicast group, from the sender's side.
class Session
{
  public:
    struct Config
    {
        std::uint8_t groupId      = 0;
        std::uint8_t senderNodeId = 0;
        std::array<std::uint8_t, 4> homeId{};
        Crypto::Key classKeyCcm{};  ///< KeyCCM of the class every member shares
    };

    Session(Config config, const MPAN::InnerState& initial)
        : config_(config),
          generator_(initial)
    {
    }

    /// Build the S2 MC frame carrying `inner`: MGRP extension unencrypted, body
    /// encrypted under the group MPAN, Destination Tag = Group ID. Advances the
    /// group's MPAN by ADVANCE_PER_ROUND.
    [[nodiscard]] auto multicastFrame(std::span<const std::uint8_t> inner,
                                      std::uint8_t sequenceNumber) -> std::vector<std::uint8_t>;

    /// The MGRP extension bytes every follow-up must carry. The caller attaches
    /// these to a SPAN-encrypted singlecast to build each S2 SC-F.
    [[nodiscard]] auto followUpExtension() const -> std::vector<std::uint8_t>;

    /// The **encrypted** MPAN extension bytes that resynchronise a member which
    /// reported MOS, carrying the group's current inner state (§4.2.6.5.14).
    /// Pass as `encryptedExtensions` on a singlecast to that member.
    [[nodiscard]] auto mpanExtension() const -> std::vector<std::uint8_t>;

    /// The group's current inner state — for persistence, and for tests.
    [[nodiscard]] auto innerState() const -> const MPAN::InnerState&
    {
        return generator_.innerState();
    }

    [[nodiscard]] auto groupId() const -> std::uint8_t
    {
        return config_.groupId;
    }

  private:
    Config config_;
    MPAN::Generator generator_;
};
}  // namespace S2::Multicast

#endif  // ZWAVED_S2_MULTICAST_SESSION_HPP
