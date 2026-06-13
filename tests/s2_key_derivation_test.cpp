// Security S2 (CC 0x9F) CKDF key derivations (#187). No published CKDF vectors,
// so these pin the spec formulas against an independent CMAC computation (CMAC
// itself is NIST-KAT-verified in s2_crypto_test) + determinism / distinctness.

#include "Crypto.hpp"
#include "KeyDerivation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace
{
const S2::Crypto::SharedSecret SHARED{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                      17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
const S2::Crypto::PublicKey CONTROLLER_PUB{0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
                                           0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5,
                                           0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF};
const S2::Crypto::PublicKey NODE_PUB{0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
                                     0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
                                     0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F};

// CMAC(prk, const·constLen ‖ index) — one CKDF expand block, chained on `prev`.
auto block(const S2::Crypto::Key& prk,
           const S2::Crypto::Mac* prev,
           std::uint8_t constByte,
           std::uint8_t index) -> S2::Crypto::Mac
{
    std::vector<std::uint8_t> msg;
    if (prev != nullptr)
    {
        msg.insert(msg.end(), prev->begin(), prev->end());
    }
    msg.insert(msg.end(), 15, constByte);
    msg.push_back(index);
    return S2::Crypto::cmac(prk, std::span<const std::uint8_t>(msg));
}
}  // namespace

TEST(S2KeyDerivation, TempExtractIsDeterministicAndOrderSensitive)
{
    EXPECT_EQ(S2::KeyDerivation::tempExtract(SHARED, CONTROLLER_PUB, NODE_PUB),
              S2::KeyDerivation::tempExtract(SHARED, CONTROLLER_PUB, NODE_PUB));
    // Swapping the public-key order changes the PRK (AAD-style ordering matters).
    EXPECT_NE(S2::KeyDerivation::tempExtract(SHARED, CONTROLLER_PUB, NODE_PUB),
              S2::KeyDerivation::tempExtract(SHARED, NODE_PUB, CONTROLLER_PUB));
}

TEST(S2KeyDerivation, TempExpandMatchesSpecFormula)
{
    const auto prk  = S2::KeyDerivation::tempExtract(SHARED, CONTROLLER_PUB, NODE_PUB);
    const auto temp = S2::KeyDerivation::tempExpand(prk);

    const auto t1 = block(prk, nullptr, 0x88, 0x01);
    const auto t2 = block(prk, &t1, 0x88, 0x02);
    const auto t3 = block(prk, &t2, 0x88, 0x03);

    EXPECT_EQ(temp.keyCcm, t1);  // TempKeyCCM = T1
    // TempPersonalizationString = T2 ‖ T3
    EXPECT_TRUE(std::equal(t2.begin(), t2.end(), temp.personalization.begin()));
    EXPECT_TRUE(std::equal(t3.begin(), t3.end(), temp.personalization.begin() + t2.size()));
}

TEST(S2KeyDerivation, NetworkKeyExpandMatchesSpecFormula)
{
    const S2::Crypto::Key networkKey{
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F};
    const auto derived = S2::KeyDerivation::networkKeyExpand(networkKey);

    const auto t1 = block(networkKey, nullptr, 0x55, 0x01);
    const auto t2 = block(networkKey, &t1, 0x55, 0x02);
    const auto t3 = block(networkKey, &t2, 0x55, 0x03);
    const auto t4 = block(networkKey, &t3, 0x55, 0x04);

    EXPECT_EQ(derived.keyCcm, t1);   // KeyCCM = T1
    EXPECT_EQ(derived.keyMpan, t4);  // KeyMPAN = T4
    EXPECT_TRUE(std::equal(t2.begin(), t2.end(), derived.personalization.begin()));
    EXPECT_TRUE(std::equal(t3.begin(), t3.end(), derived.personalization.begin() + t2.size()));
}

TEST(S2KeyDerivation, DistinctInputsDeriveDistinctKeys)
{
    const auto temp  = S2::KeyDerivation::deriveTempKeys(SHARED, CONTROLLER_PUB, NODE_PUB);
    auto otherShared = SHARED;
    otherShared.at(0) ^= 0x01;
    const auto temp2 = S2::KeyDerivation::deriveTempKeys(otherShared, CONTROLLER_PUB, NODE_PUB);
    EXPECT_NE(temp.keyCcm, temp2.keyCcm);

    const S2::Crypto::Key keyA{0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const S2::Crypto::Key keyB{0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_NE(S2::KeyDerivation::networkKeyExpand(keyA).keyCcm, S2::KeyDerivation::networkKeyExpand(keyB).keyCcm);
}
