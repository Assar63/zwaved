// Security S2 MPAN (#188) — the multicast pre-agreed nonce: NextMPAN
// generation, the MPAN table's entry lifecycle, and the MPAN / MGRP extension
// codecs (SDS13783 §4.2.6.4.22-23, §4.2.6.5.4, §4.2.6.5.14-15).
//
// There are no published MPAN test vectors, so NextMPAN is pinned the way the
// CKDF tests pin their formulas: by computing the spec's steps from a known
// AES-128 vector (FIPS-197 §C.1) and asserting the implementation agrees. That
// catches a wrong truncation, a wrong key, or a missing increment — the three
// ways this can silently desynchronise a whole multicast group.

#include "Crypto.hpp"
#include "Mpan.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
// FIPS-197 §C.1 AES-128:
//   key        000102030405060708090a0b0c0d0e0f
//   plaintext  00112233445566778899aabbccddeeff
//   ciphertext 69c4e0d86a7b0430d8cdb78070b4c55a
const S2::Crypto::Key KEY_MPAN{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
const S2::MPAN::InnerState STATE{
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
// The 13 most significant bytes of the FIPS-197 ciphertext.
const S2::Encapsulation::CcmNonce EXPECTED_NONCE{
    0x69, 0xC4, 0xE0, 0xD8, 0x6A, 0x7B, 0x04, 0x30, 0xD8, 0xCD, 0xB7, 0x80, 0x70};

constexpr std::uint8_t OWNER    = 3;
constexpr std::uint8_t GROUP_ID = 7;
}  // namespace

// §4.2.6.4.23 step 2-3: AES-128-ECB under KeyMPAN, truncated to 13 bytes.
TEST(S2Mpan, NextNonceMatchesSpecFormula)
{
    S2::MPAN::Generator generator(STATE);
    EXPECT_EQ(generator.nextNonce(KEY_MPAN), EXPECTED_NONCE);
}

// §4.2.6.4.23 step 4: the inner state is incremented after each nonce.
TEST(S2Mpan, NextNonceAdvancesInnerState)
{
    S2::MPAN::Generator generator(STATE);
    static_cast<void>(generator.nextNonce(KEY_MPAN));

    S2::MPAN::InnerState expected = STATE;
    expected[15]                  = 0x00;  // 0xFF + 1 carries...
    expected[14]                  = 0xEF;  // ...into the next byte (0xEE -> 0xEF)
    EXPECT_EQ(generator.innerState(), expected);
}

// "incrementing all ones MUST yield all zeros" — the wraparound the spec calls
// out explicitly.
TEST(S2Mpan, InnerStateWrapsFromAllOnesToAllZeros)
{
    S2::MPAN::InnerState allOnes{};
    allOnes.fill(0xFF);
    S2::MPAN::Generator generator(allOnes);

    generator.advance();

    S2::MPAN::InnerState allZeros{};
    EXPECT_EQ(generator.innerState(), allZeros);
}

// The whole point of an MPAN: every group member derives the same sequence, so
// one encrypted broadcast decrypts for all of them.
TEST(S2Mpan, SenderAndReceiversStayInLockstep)
{
    S2::MPAN::Generator sender(STATE);
    S2::MPAN::Generator receiverA(STATE);
    S2::MPAN::Generator receiverB(STATE);

    for (int frame = 0; frame < 8; ++frame)
    {
        const auto sent = sender.nextNonce(KEY_MPAN);
        EXPECT_EQ(receiverA.nextNonce(KEY_MPAN), sent);
        EXPECT_EQ(receiverB.nextNonce(KEY_MPAN), sent);
    }
    EXPECT_EQ(sender.innerState(), receiverA.innerState());
}

// A receiver that misses one frame produces a different nonce from then on —
// which is exactly what puts it into MOS and triggers resynchronisation.
TEST(S2Mpan, AMissedFrameDesynchronisesTheReceiver)
{
    S2::MPAN::Generator sender(STATE);
    S2::MPAN::Generator receiver(STATE);

    static_cast<void>(sender.nextNonce(KEY_MPAN));  // receiver misses this one

    EXPECT_NE(sender.nextNonce(KEY_MPAN), receiver.nextNonce(KEY_MPAN));
}

// ---- MPAN table ------------------------------------------------------

TEST(S2MpanTable, SetMakesTheGroupUsable)
{
    S2::MPAN::Table table;
    EXPECT_FALSE(table.contains(OWNER, GROUP_ID));
    EXPECT_EQ(table.entryState(OWNER, GROUP_ID), S2::MPAN::EntryState::Free);
    EXPECT_EQ(table.find(OWNER, GROUP_ID), nullptr);

    table.set(OWNER, GROUP_ID, STATE);

    EXPECT_TRUE(table.contains(OWNER, GROUP_ID));
    EXPECT_EQ(table.entryState(OWNER, GROUP_ID), S2::MPAN::EntryState::Used);
    auto* generator = table.find(OWNER, GROUP_ID);
    ASSERT_NE(generator, nullptr);
    EXPECT_EQ(generator->innerState(), STATE);
}

// A group is identified by (owner, groupId) — the same group number from a
// different sender is a different MPAN entirely (§4.2.6.5.3).
TEST(S2MpanTable, GroupsAreScopedByOwner)
{
    S2::MPAN::Table table;
    table.set(OWNER, GROUP_ID, STATE);

    EXPECT_TRUE(table.contains(OWNER, GROUP_ID));
    EXPECT_FALSE(table.contains(OWNER + 1, GROUP_ID));
    EXPECT_EQ(table.find(OWNER + 1, GROUP_ID), nullptr);
}

// An out-of-sync entry must not be handed back for use: its state is stale, and
// encrypting with it would produce a nonce no one else can follow.
TEST(S2MpanTable, OutOfSyncEntryIsNotUsable)
{
    S2::MPAN::Table table;
    table.set(OWNER, GROUP_ID, STATE);

    table.markOutOfSync(OWNER, GROUP_ID);

    EXPECT_EQ(table.entryState(OWNER, GROUP_ID), S2::MPAN::EntryState::Mos);
    EXPECT_FALSE(table.contains(OWNER, GROUP_ID));
    EXPECT_EQ(table.find(OWNER, GROUP_ID), nullptr);
}

// A frame for a group we've never heard of still has to be recorded, so the
// next singlecast can answer with the MOS flag.
TEST(S2MpanTable, MarkingAnUnknownGroupCreatesAMosEntry)
{
    S2::MPAN::Table table;
    table.markOutOfSync(OWNER, GROUP_ID);

    EXPECT_EQ(table.entryState(OWNER, GROUP_ID), S2::MPAN::EntryState::Mos);
    EXPECT_EQ(table.size(), 1U);
}

// Resynchronisation: a fresh MPAN extension puts a MOS entry back in service.
TEST(S2MpanTable, SetClearsOutOfSync)
{
    S2::MPAN::Table table;
    table.markOutOfSync(OWNER, GROUP_ID);

    table.set(OWNER, GROUP_ID, STATE);

    EXPECT_EQ(table.entryState(OWNER, GROUP_ID), S2::MPAN::EntryState::Used);
    ASSERT_NE(table.find(OWNER, GROUP_ID), nullptr);
    EXPECT_EQ(table.find(OWNER, GROUP_ID)->innerState(), STATE);
}

TEST(S2MpanTable, RemoveForgetsTheGroup)
{
    S2::MPAN::Table table;
    table.set(OWNER, GROUP_ID, STATE);

    table.remove(OWNER, GROUP_ID);

    EXPECT_EQ(table.size(), 0U);
    EXPECT_EQ(table.entryState(OWNER, GROUP_ID), S2::MPAN::EntryState::Free);
}

// ---- Extension codecs ------------------------------------------------

// §4.2.6.5.14: [19][0x42][groupId][inner·16].
TEST(S2MpanExtension, MpanExtensionHasSpecShape)
{
    const auto encoded = S2::MPAN::encodeMpanExtension(GROUP_ID, STATE);

    ASSERT_EQ(encoded.size(), 19U);
    EXPECT_EQ(encoded[0], 19);    // length
    EXPECT_EQ(encoded[1], 0x42);  // critical=1, type=MPAN(2)
    EXPECT_EQ(encoded[2], GROUP_ID);
    EXPECT_TRUE(std::equal(STATE.begin(), STATE.end(), encoded.begin() + 3));
}

TEST(S2MpanExtension, MpanExtensionRoundTrips)
{
    const auto encoded = S2::MPAN::encodeMpanExtension(GROUP_ID, STATE);
    const auto decoded = S2::MPAN::findMpanExtension(std::span<const std::uint8_t>(encoded));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->first, GROUP_ID);
    EXPECT_EQ(decoded->second, STATE);
}

// §4.2.6.5.15: [3][0x43][groupId].
TEST(S2MpanExtension, MgrpExtensionHasSpecShape)
{
    const auto encoded = S2::MPAN::encodeMgrpExtension(GROUP_ID);

    ASSERT_EQ(encoded.size(), 3U);
    EXPECT_EQ(encoded[0], 3);
    EXPECT_EQ(encoded[1], 0x43);  // critical=1, type=MGRP(3)
    EXPECT_EQ(encoded[2], GROUP_ID);

    const auto decoded = S2::MPAN::findMgrpExtension(std::span<const std::uint8_t>(encoded));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, GROUP_ID);
}

// The scanners must not confuse the two types, nor match a SPAN extension.
TEST(S2MpanExtension, ScannersIgnoreTheOtherExtensionType)
{
    const auto mgrp = S2::MPAN::encodeMgrpExtension(GROUP_ID);
    EXPECT_FALSE(S2::MPAN::findMpanExtension(std::span<const std::uint8_t>(mgrp)).has_value());

    const auto mpan = S2::MPAN::encodeMpanExtension(GROUP_ID, STATE);
    EXPECT_FALSE(S2::MPAN::findMgrpExtension(std::span<const std::uint8_t>(mpan)).has_value());
}

// A chain: the scanner must walk past an earlier object (more-to-follow set)
// to find the one it wants.
TEST(S2MpanExtension, FindsAnExtensionLaterInAChain)
{
    // A leading SPAN-shaped object (type 1) with more-to-follow set, then MGRP.
    std::vector<std::uint8_t> chain{18, 0xC1};  // moreToFollow=1, critical=1, type=1
    chain.resize(18, 0xAA);                     // 16 bytes of payload
    const auto mgrp = S2::MPAN::encodeMgrpExtension(GROUP_ID);
    chain.insert(chain.end(), mgrp.begin(), mgrp.end());

    const auto decoded = S2::MPAN::findMgrpExtension(std::span<const std::uint8_t>(chain));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, GROUP_ID);
}

// Without more-to-follow, nothing after the first object is part of the chain.
TEST(S2MpanExtension, StopsAtTheEndOfTheChain)
{
    std::vector<std::uint8_t> chain{18, 0x41};  // moreToFollow=0 — chain ends here
    chain.resize(18, 0xAA);
    const auto mgrp = S2::MPAN::encodeMgrpExtension(GROUP_ID);
    chain.insert(chain.end(), mgrp.begin(), mgrp.end());

    EXPECT_FALSE(S2::MPAN::findMgrpExtension(std::span<const std::uint8_t>(chain)).has_value());
}

// Malformed input is dropped, not trusted — these arrive over the air.
TEST(S2MpanExtension, MalformedChainsYieldNothing)
{
    const std::vector<std::uint8_t> empty;
    EXPECT_FALSE(S2::MPAN::findMpanExtension(std::span<const std::uint8_t>(empty)).has_value());

    const std::vector<std::uint8_t> truncated{19, 0x42, GROUP_ID, 0x00};  // claims 19, has 4
    EXPECT_FALSE(S2::MPAN::findMpanExtension(std::span<const std::uint8_t>(truncated)).has_value());

    const std::vector<std::uint8_t> zeroLength{0, 0x42};
    EXPECT_FALSE(S2::MPAN::findMpanExtension(std::span<const std::uint8_t>(zeroLength)).has_value());

    // Right type, wrong size — must not be read as a valid MPAN extension.
    const std::vector<std::uint8_t> shortMpan{4, 0x42, GROUP_ID, 0x00};
    EXPECT_FALSE(S2::MPAN::findMpanExtension(std::span<const std::uint8_t>(shortMpan)).has_value());
}
