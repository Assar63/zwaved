#ifndef ZWAVED_TRANSPORT_SERVICE_HPP
#define ZWAVED_TRANSPORT_SERVICE_HPP

// IWYU pragma: begin_exports
#include "TransportService.gen.hpp"
// IWYU pragma: end_exports

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Z-Wave Transport Service Command Class (0x55) — segmentation for
/// datagrams longer than the radio MTU (#25). A datagram (itself a CC frame)
/// is split into a FIRST_SEGMENT + SUBSEQUENT_SEGMENTs, each with an 11-bit
/// datagram size, a 4-bit session id, an 11-bit datagram offset (subsequent
/// only) and a 2-byte CRC-16 Frame Check Sequence. Transport-only: invisible
/// above the protocol layer. Constants come from TransportService.gen.hpp;
/// the split encoder + the stateful reassembler are hand-written.
namespace TransportService
{
/// Max payload bytes carried per segment. Conservative so a full segment
/// (header + payload + 2-byte FCS) stays within the Z-Wave MTU. Not
/// spec-critical for reassembly — the receiver reads the datagram size from
/// the frames — so it can be tuned without breaking interop.
constexpr std::size_t SEGMENT_PAYLOAD_MAX = 39;

/// The Frame Check Sequence: CRC-16-CCITT (poly 0x1021) over the whole
/// segment up to (not including) the FCS. Seeded 0x1D0F per Z-Wave
/// convention. NOTE: confirm the seed against SDS13783 before relying on
/// real-device interop — self-consistent split/reassemble is unaffected.
[[nodiscard]] auto fcs(std::span<const std::uint8_t> data) -> std::uint16_t;

/// Split a full datagram (an inner CC frame) into Transport Service segments
/// for `sessionId` (low 4 bits used). Returns FIRST_SEGMENT followed by N
/// SUBSEQUENT_SEGMENTs, each FCS-trailed. An empty datagram yields no
/// segments.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): datagram / sessionId are distinct at call sites
[[nodiscard]] auto splitOutbound(std::span<const std::uint8_t> datagram,
                                 std::uint8_t sessionId) -> std::vector<std::vector<std::uint8_t>>;

/// Stateful reassembler for one peer's in-flight datagram (MVP: a single
/// session at a time). Segments may arrive out of order — each is placed at
/// its datagram offset and the datagram completes once every byte is filled.
/// Deferred to follow-ups: SEGMENT_REQUEST for genuinely lost segments,
/// inactivity timeout, and concurrent sessions.
class Assembler
{
  public:
    /// Feed one raw Transport Service segment (starting with COMMAND_CLASS).
    /// Returns the fully reassembled datagram (the inner CC frame) when the
    /// last missing byte arrives, else std::nullopt. A non-0x55 frame, a bad
    /// FCS, or an incomplete datagram all return std::nullopt.
    [[nodiscard]] auto feedSegment(std::span<const std::uint8_t> segment) -> std::optional<std::vector<std::uint8_t>>;

    /// Drop any in-flight datagram (e.g. on session timeout).
    auto reset() -> void;

  private:
    /// Handle a FIRST_SEGMENT: (re)start a datagram and place its payload.
    [[nodiscard]] auto handleFirst(std::span<const std::uint8_t> segment,
                                   std::size_t bodyLen) -> std::optional<std::vector<std::uint8_t>>;

    /// Handle a SUBSEQUENT_SEGMENT: place its payload at the carried offset.
    [[nodiscard]] auto handleSubsequent(std::span<const std::uint8_t> segment,
                                        std::size_t bodyLen) -> std::optional<std::vector<std::uint8_t>>;

    /// Resolve where the payload starts in `segment`, skipping the optional
    /// header extension when the Ext flag is set; std::nullopt if malformed.
    [[nodiscard]] static auto payloadStart(std::span<const std::uint8_t> segment,
                                           std::size_t bodyLen,
                                           std::size_t baseHeader) -> std::optional<std::size_t>;

    /// Copy `payload` into the reassembly buffer at `offset`, counting only
    /// newly-filled bytes; returns the datagram once every byte is present.
    [[nodiscard]] auto place(std::size_t offset,
                             std::span<const std::uint8_t> payload) -> std::optional<std::vector<std::uint8_t>>;

    bool active_               = false;
    std::uint8_t sessionId_    = 0;
    std::size_t datagramSize_  = 0;
    std::size_t receivedCount_ = 0;
    std::vector<std::uint8_t> buffer_;
    std::vector<bool> received_;
};
}  // namespace TransportService

#endif  // ZWAVED_TRANSPORT_SERVICE_HPP
