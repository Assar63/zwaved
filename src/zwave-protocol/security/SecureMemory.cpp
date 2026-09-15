#include "SecureMemory.hpp"

#include <cstdint>
#include <span>

#include <openssl/crypto.h>

auto Secrets::secureZero(std::span<std::uint8_t> buffer) -> void
{
    if (buffer.empty())
    {
        return;
    }
    // OPENSSL_cleanse exists specifically so the compiler cannot elide a write
    // to storage whose lifetime is ending — which is exactly what it would be
    // entitled to do with std::fill or memset here.
    OPENSSL_cleanse(buffer.data(), buffer.size());
}

auto Secrets::constantTimeEquals(std::span<const std::uint8_t> lhs, std::span<const std::uint8_t> rhs) -> bool
{
    if (lhs.size() != rhs.size())
    {
        return false;  // a length is not secret; a mismatch here is a bug, not an attack
    }
    if (lhs.empty())
    {
        return true;
    }
    // CRYPTO_memcmp returns 0 when equal — note the inverted sense against the
    // std::equal call this replaced.
    return CRYPTO_memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}
