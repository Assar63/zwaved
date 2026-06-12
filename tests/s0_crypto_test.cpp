#include "Crypto.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

// Authoritative known-answer vectors for the raw AES primitives come from
// FIPS-197 (ECB) and NIST SP800-38A (OFB). The S0-specific pieces
// (key derivation, CBC-MAC) are cross-checked against the FIPS-validated
// block cipher — SDS10865 doesn't publish byte vectors we can cite verbatim.

namespace
{
using S0::Crypto::Block;
using S0::Crypto::Key;

// FIPS-197 Appendix C.1 — AES-128 single-block encryption.
constexpr Key FIPS_KEY{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr Block FIPS_IN{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
constexpr Block FIPS_OUT{
    0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};

// NIST SP800-38A F.4 — OFB-AES128.
constexpr Key OFB_KEY{0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
constexpr Block OFB_IV{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr std::array<std::uint8_t, 64> OFB_PLAINTEXT{
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
    0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
    0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};
constexpr std::array<std::uint8_t, 64> OFB_CIPHERTEXT{
    0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20, 0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a,
    0x77, 0x89, 0x50, 0x8d, 0x16, 0x91, 0x8f, 0x03, 0xf5, 0x3c, 0x52, 0xda, 0xc5, 0x4e, 0xd8, 0x25,
    0x97, 0x40, 0x05, 0x1e, 0x9c, 0x5f, 0xec, 0xf6, 0x43, 0x44, 0xf7, 0xa8, 0x22, 0x60, 0xed, 0xcc,
    0x30, 0x4c, 0x65, 0x28, 0xf6, 0x59, 0xc7, 0x78, 0x66, 0xa5, 0x10, 0xd9, 0xc1, 0xd6, 0xae, 0x5e};

// Reference CBC-MAC built from the FIPS-validated single-block primitive, to
// independently confirm S0::Crypto::cbcMac.
auto referenceCbcMac(const Key& key, std::span<const std::uint8_t> data) -> std::array<std::uint8_t, 8>
{
    constexpr std::size_t blockSize = 16;
    Block state{};
    const std::size_t padded = data.empty() ? blockSize : ((data.size() + blockSize - 1) / blockSize) * blockSize;
    for (std::size_t off = 0; off < padded; off += blockSize)
    {
        Block blk{};
        for (std::size_t i = 0; i < blockSize && off + i < data.size(); ++i)
        {
            blk[i] = data[off + i];
        }
        for (std::size_t i = 0; i < blockSize; ++i)
        {
            blk[i] ^= state[i];
        }
        state = S0::Crypto::ecbEncryptBlock(key, blk);
    }
    std::array<std::uint8_t, 8> mac{};
    std::copy(state.begin(), state.begin() + 8, mac.begin());
    return mac;
}
}  // namespace

TEST(S0Crypto, EcbEncryptBlockMatchesFips197)
{
    EXPECT_EQ(S0::Crypto::ecbEncryptBlock(FIPS_KEY, FIPS_IN), FIPS_OUT);
}

TEST(S0Crypto, OfbMatchesNistSp800_38a)
{
    const auto out = S0::Crypto::ofbCrypt(OFB_KEY, OFB_IV, std::span<const std::uint8_t>(OFB_PLAINTEXT));
    EXPECT_EQ(out, std::vector<std::uint8_t>(OFB_CIPHERTEXT.begin(), OFB_CIPHERTEXT.end()));
}

TEST(S0Crypto, OfbIsSymmetric)
{
    // Feeding the ciphertext back through the same call recovers the plaintext.
    const auto recovered = S0::Crypto::ofbCrypt(OFB_KEY, OFB_IV, std::span<const std::uint8_t>(OFB_CIPHERTEXT));
    EXPECT_EQ(recovered, std::vector<std::uint8_t>(OFB_PLAINTEXT.begin(), OFB_PLAINTEXT.end()));
}

TEST(S0Crypto, CbcMacSingleBlockEqualsEcbPrefix)
{
    // For a one-block message with IV=0, CBC-MAC = the leading 8 bytes of
    // ECB(key, block) — pinned to the FIPS-197 known answer.
    const auto mac = S0::Crypto::cbcMac(FIPS_KEY, std::span<const std::uint8_t>(FIPS_IN));
    EXPECT_TRUE(std::equal(mac.begin(), mac.end(), FIPS_OUT.begin()));
}

TEST(S0Crypto, CbcMacMultiBlockMatchesReferenceChain)
{
    // 37 bytes — deliberately not a block multiple, to exercise zero-padding.
    std::vector<std::uint8_t> data(37);
    for (std::size_t i = 0; i < data.size(); ++i)
    {
        data[i] = static_cast<std::uint8_t>(i * 7 + 1);
    }
    const auto mac = S0::Crypto::cbcMac(FIPS_KEY, std::span<const std::uint8_t>(data));
    const auto ref = referenceCbcMac(FIPS_KEY, std::span<const std::uint8_t>(data));
    EXPECT_EQ(mac, ref);
}

TEST(S0Crypto, DeriveKeysUsesEcbWithS0Constants)
{
    const auto keys = S0::Crypto::deriveKeys(FIPS_KEY);
    Block encSeed{};
    Block authSeed{};
    encSeed.fill(0xAA);
    authSeed.fill(0x55);
    EXPECT_EQ(keys.encryption, S0::Crypto::ecbEncryptBlock(FIPS_KEY, encSeed));
    EXPECT_EQ(keys.authentication, S0::Crypto::ecbEncryptBlock(FIPS_KEY, authSeed));
    // Ke and Ka must differ — the two derivation constants are distinct.
    EXPECT_NE(keys.encryption, keys.authentication);
}

TEST(S0Crypto, EncryptDecryptRoundTripWithDerivedKey)
{
    const auto keys = S0::Crypto::deriveKeys(FIPS_KEY);
    const std::vector<std::uint8_t> payload{0x98, 0x81, 0x25, 0x01, 0xFF, 0x00, 0x42};
    const auto cipher = S0::Crypto::ofbCrypt(keys.encryption, OFB_IV, std::span<const std::uint8_t>(payload));
    EXPECT_NE(cipher, payload);
    const auto plain = S0::Crypto::ofbCrypt(keys.encryption, OFB_IV, std::span<const std::uint8_t>(cipher));
    EXPECT_EQ(plain, payload);
}
