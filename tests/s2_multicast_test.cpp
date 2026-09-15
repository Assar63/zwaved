// Security S2 multicast send sequencing (#188) — SDS13783 §4.2.6.5.3-7.
//
// The tests cross-drive a receiver: the Session builds the S2 MC frame, and the
// test decrypts it the way a group member would (its own MPAN generator, the
// Group ID as the AAD Destination Tag). That proves the two sides agree on the
// nonce, the key and the identity binding — the three things a multicast frame
// can silently disagree about.

#include "Crypto.hpp"
#include "Encapsulation.hpp"
#include "Mpan.hpp"
#include "MulticastSession.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
constexpr std::uint8_t CONTROLLER = 1;
constexpr std::uint8_t GROUP_ID   = 3;

const S2::Crypto::Key CLASS_KEY{
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
const std::array<std::uint8_t, 4> HOME{0xDE, 0xAD, 0xBE, 0xEF};
const S2::MPAN::InnerState START{
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F};

auto makeSession() -> S2::Multicast::Session
{
    return S2::Multicast::Session(
        S2::Multicast::Session::Config{
            .groupId = GROUP_ID, .senderNodeId = CONTROLLER, .homeId = HOME, .classKeyCcm = CLASS_KEY},
        START);
}

// What a group member does on receipt: draw its own next MPAN nonce and decrypt
// with the Group ID as the Destination Tag.
auto receiverDecrypt(S2::MPAN::Generator& member,
                     std::span<const std::uint8_t> frame) -> std::optional<std::vector<std::uint8_t>>
{
    const auto nonce = member.nextNonce(CLASS_KEY);
    return S2::Encapsulation::decrypt(
        frame,
        S2::Encapsulation::Context{
            .senderNodeId = CONTROLLER, .receiverNodeId = GROUP_ID, .homeId = HOME, .sequenceNumber = frame[2]},
        CLASS_KEY,
        nonce);
}
}  // namespace

// The core claim: a member holding the same MPAN decrypts the multicast.
TEST(S2Multicast, GroupMemberDecryptsTheMulticastFrame)
{
    auto session = makeSession();
    S2::MPAN::Generator member(START);

    const std::vector<std::uint8_t> inner{0x20, 0x01, 0xFF};  // Basic Set on
    const auto frame = session.multicastFrame(std::span<const std::uint8_t>(inner), 7);

    const auto decoded = receiverDecrypt(member, std::span<const std::uint8_t>(frame));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, inner);
}

// Every member of the group derives the same nonce, so one frame serves all —
// the entire point of multicast.
TEST(S2Multicast, EveryMemberDecryptsTheSameFrame)
{
    auto session = makeSession();
    S2::MPAN::Generator memberA(START);
    S2::MPAN::Generator memberB(START);
    S2::MPAN::Generator memberC(START);

    const std::vector<std::uint8_t> inner{0x25, 0x01, 0x00};
    const auto frame = session.multicastFrame(std::span<const std::uint8_t>(inner), 9);

    for (auto* member : {&memberA, &memberB, &memberC})
    {
        const auto decoded = receiverDecrypt(*member, std::span<const std::uint8_t>(frame));
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(*decoded, inner);
    }
}

// The frame must carry the MGRP extension unencrypted, or a receiver cannot
// tell which MPAN to reach for (§4.2.6.5.15).
TEST(S2Multicast, MulticastFrameCarriesTheMgrpExtension)
{
    auto session = makeSession();
    const std::vector<std::uint8_t> inner{0x20, 0x01, 0xFF};
    const auto frame = session.multicastFrame(std::span<const std::uint8_t>(inner), 1);

    ASSERT_GT(frame.size(), 4U);
    EXPECT_EQ(frame[0], 0x9F);
    EXPECT_EQ(frame[1], 0x03);      // MESSAGE_ENCAPSULATION
    EXPECT_NE(frame[3] & 0x01, 0);  // props bit0 — non-encrypted extensions present

    const auto groupId = S2::MPAN::findMgrpExtension(std::span<const std::uint8_t>(frame).subspan(4));
    ASSERT_TRUE(groupId.has_value());
    EXPECT_EQ(*groupId, GROUP_ID);
}

// Figure 4.17: the sender's MPAN runs #N → #N+2 → #N+4 across rounds. This is
// the constant the whole group's synchronisation hangs on, and the spec's own
// Table 4.9 disagrees with it (says 3) — see MulticastSession.hpp. If real
// hardware desyncs on the second multicast, start here.
TEST(S2Multicast, SenderAdvancesMpanByTwoPerRound)
{
    auto session = makeSession();
    S2::MPAN::Generator reference(START);

    const std::vector<std::uint8_t> inner{0x20, 0x01, 0xFF};
    static_cast<void>(session.multicastFrame(std::span<const std::uint8_t>(inner), 1));

    reference.advance();
    reference.advance();
    EXPECT_EQ(session.innerState(), reference.innerState());

    static_cast<void>(session.multicastFrame(std::span<const std::uint8_t>(inner), 2));
    reference.advance();
    reference.advance();
    EXPECT_EQ(session.innerState(), reference.innerState());
}

// A member whose MPAN has drifted must not decrypt — that is what puts it into
// MOS and triggers the resync push.
TEST(S2Multicast, DesyncedMemberFailsToDecrypt)
{
    auto session = makeSession();
    S2::MPAN::Generator member(START);
    member.advance();  // drifted by one

    const std::vector<std::uint8_t> inner{0x20, 0x01, 0xFF};
    const auto frame = session.multicastFrame(std::span<const std::uint8_t>(inner), 4);

    EXPECT_FALSE(receiverDecrypt(member, std::span<const std::uint8_t>(frame)).has_value());
}

// The MOS repair: an encrypted MPAN extension carrying the group's current
// state, which the member installs to come back into sync.
TEST(S2Multicast, MpanExtensionCarriesCurrentState)
{
    auto session = makeSession();
    const std::vector<std::uint8_t> inner{0x20, 0x01, 0xFF};
    static_cast<void>(session.multicastFrame(std::span<const std::uint8_t>(inner), 1));

    const auto extension = session.mpanExtension();
    const auto decoded   = S2::MPAN::findMpanExtension(std::span<const std::uint8_t>(extension));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->first, GROUP_ID);
    EXPECT_EQ(decoded->second, session.innerState());
}

// End to end: a member that missed the first round is repaired by the MPAN push
// and then decrypts the next multicast — the §4.2.6.5.3 self-healing path.
TEST(S2Multicast, MpanPushResynchronisesALostMember)
{
    auto session = makeSession();
    S2::MPAN::Generator member(START);

    const std::vector<std::uint8_t> inner{0x20, 0x01, 0xFF};
    static_cast<void>(session.multicastFrame(std::span<const std::uint8_t>(inner), 1));
    // The member missed that frame entirely, so it is now behind by a round.

    // The controller pushes its current MPAN state; the member installs it.
    const auto extension = session.mpanExtension();
    const auto pushed    = S2::MPAN::findMpanExtension(std::span<const std::uint8_t>(extension));
    ASSERT_TRUE(pushed.has_value());
    member = S2::MPAN::Generator(pushed->second);

    const auto next    = session.multicastFrame(std::span<const std::uint8_t>(inner), 2);
    const auto decoded = receiverDecrypt(member, std::span<const std::uint8_t>(next));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, inner);
}

// The encrypted-extension path added for the MPAN push must round-trip, and the
// extension must not leak into the plaintext the receiver sees.
TEST(S2Encapsulation, EncryptedExtensionsRoundTripAndAreStripped)
{
    S2::MPAN::Generator sender(START);
    S2::MPAN::Generator receiver(START);

    const std::vector<std::uint8_t> inner{0x20, 0x01, 0xFF};
    const auto mpanExt = S2::MPAN::encodeMpanExtension(GROUP_ID, START);

    const S2::Encapsulation::Context context{
        .senderNodeId = CONTROLLER, .receiverNodeId = 5, .homeId = HOME, .sequenceNumber = 3};

    const auto frame = S2::Encapsulation::encrypt(std::span<const std::uint8_t>(inner),
                                                  context,
                                                  CLASS_KEY,
                                                  sender.nextNonce(CLASS_KEY),
                                                  {},
                                                  std::span<const std::uint8_t>(mpanExt));

    EXPECT_NE(frame[3] & 0x02, 0);  // props bit1 — encrypted extensions present

    const auto decoded = S2::Encapsulation::decrypt(
        std::span<const std::uint8_t>(frame), context, CLASS_KEY, receiver.nextNonce(CLASS_KEY));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, inner);  // the extension is stripped, not handed to the decoder
}
