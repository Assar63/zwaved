#include "MulticastSession.hpp"

#include "Encapsulation.hpp"
#include "Mpan.hpp"

#include <cstdint>
#include <span>
#include <vector>

auto S2::Multicast::Session::multicastFrame(std::span<const std::uint8_t> inner,
                                            std::uint8_t sequenceNumber) -> std::vector<std::uint8_t>
{
    const auto mgrp = followUpExtension();

    // NextMPAN consumes one state; the AAD's Destination Tag carries the Group
    // ID rather than a node id (§4.2.6.4.6), which is what makes this a
    // multicast frame as far as authentication is concerned.
    const auto nonce = generator_.nextNonce(config_.classKeyCcm);

    const Encapsulation::Context context{.senderNodeId   = config_.senderNodeId,
                                         .receiverNodeId = config_.groupId,
                                         .homeId         = config_.homeId,
                                         .sequenceNumber = sequenceNumber};

    auto frame =
        Encapsulation::encrypt(inner, context, config_.classKeyCcm, nonce, std::span<const std::uint8_t>(mgrp), {});

    // nextNonce() already advanced by one; bring the round's total up to
    // ADVANCE_PER_ROUND (Figure 4.17 — see the header on the Table 4.9 conflict).
    for (unsigned step = 1; step < ADVANCE_PER_ROUND; ++step)
    {
        generator_.advance();
    }
    return frame;
}

auto S2::Multicast::Session::followUpExtension() const -> std::vector<std::uint8_t>
{
    return MPAN::encodeMgrpExtension(config_.groupId);
}

auto S2::Multicast::Session::mpanExtension() const -> std::vector<std::uint8_t>
{
    return MPAN::encodeMpanExtension(config_.groupId, generator_.innerState());
}
