// Secret-handling primitives (#238): constant-time comparison and zeroization.
//
// These are hygiene measures, which makes them awkward to test: the property
// that matters for constantTimeEquals is *timing*, and timing assertions are
// famously flaky in CI. So these tests pin the observable contract — the
// comparison's results, and that secureZero actually clears — and leave the
// timing property to the choice of primitive (CRYPTO_memcmp), which is where it
// belongs.

#include "SecureMemory.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

TEST(SecureMemory, ConstantTimeEqualsMatchesValueEquality)
{
    const std::array<std::uint8_t, 8> lhs{1, 2, 3, 4, 5, 6, 7, 8};
    const std::array<std::uint8_t, 8> same{1, 2, 3, 4, 5, 6, 7, 8};

    EXPECT_TRUE(Secrets::constantTimeEquals(std::span<const std::uint8_t>(lhs), std::span<const std::uint8_t>(same)));
}

// The cases that matter for a MAC check: a difference in the *first* byte and a
// difference in the *last* must both be rejected. A short-circuiting compare
// gets both right too — what differs is how long it takes — so this is the
// contract test, not the timing test.
TEST(SecureMemory, ConstantTimeEqualsRejectsDifferenceAtEitherEnd)
{
    const std::array<std::uint8_t, 8> reference{1, 2, 3, 4, 5, 6, 7, 8};
    const std::array<std::uint8_t, 8> firstByteDiffers{9, 2, 3, 4, 5, 6, 7, 8};
    const std::array<std::uint8_t, 8> lastByteDiffers{1, 2, 3, 4, 5, 6, 7, 9};

    EXPECT_FALSE(Secrets::constantTimeEquals(std::span<const std::uint8_t>(reference),
                                             std::span<const std::uint8_t>(firstByteDiffers)));
    EXPECT_FALSE(Secrets::constantTimeEquals(std::span<const std::uint8_t>(reference),
                                             std::span<const std::uint8_t>(lastByteDiffers)));
}

TEST(SecureMemory, ConstantTimeEqualsRejectsLengthMismatch)
{
    const std::array<std::uint8_t, 8> eight{1, 2, 3, 4, 5, 6, 7, 8};
    const std::array<std::uint8_t, 4> four{1, 2, 3, 4};

    EXPECT_FALSE(
        Secrets::constantTimeEquals(std::span<const std::uint8_t>(eight), std::span<const std::uint8_t>(four)));
}

TEST(SecureMemory, ConstantTimeEqualsTreatsEmptyAsEqual)
{
    const std::vector<std::uint8_t> empty;
    EXPECT_TRUE(
        Secrets::constantTimeEquals(std::span<const std::uint8_t>(empty), std::span<const std::uint8_t>(empty)));
}

TEST(SecureMemory, SecureZeroClearsTheBuffer)
{
    std::array<std::uint8_t, 16> secret{};
    secret.fill(0xA5);

    Secrets::secureZero(std::span<std::uint8_t>(secret));

    const std::array<std::uint8_t, 16> zeros{};
    EXPECT_EQ(secret, zeros);
}

TEST(SecureMemory, SecureZeroToleratesAnEmptyBuffer)
{
    std::vector<std::uint8_t> empty;
    Secrets::secureZero(std::span<std::uint8_t>(empty));  // must not crash
    EXPECT_TRUE(empty.empty());
}

// SecretBytes is the RAII form: it cleanses its own storage when it dies. The
// destructor's effect can't be observed after the object is gone without
// reading freed memory, so instead check the two things that are observable —
// it round-trips its value, and an explicit scrub of its storage clears it.
TEST(SecureMemory, SecretBytesRoundTripsAndScrubs)
{
    const std::array<std::uint8_t, 16> material{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    Secrets::SecretBytes<16> secret(material);

    EXPECT_EQ(secret.bytes(), material);

    Secrets::secureZero(std::span<std::uint8_t>(secret.mutableBytes()));
    const std::array<std::uint8_t, 16> zeros{};
    EXPECT_EQ(secret.bytes(), zeros);
}

TEST(SecureMemory, SecretBytesDefaultsToZero)
{
    const Secrets::SecretBytes<16> secret;
    const std::array<std::uint8_t, 16> zeros{};
    EXPECT_EQ(secret.bytes(), zeros);
}
