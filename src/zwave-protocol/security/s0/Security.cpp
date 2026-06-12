#include "Security.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

auto Security::encodeNonceGet() -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, SECURITY_NONCE_GET};
}

auto Security::encodeNonceReport(const S0::Nonce& nonce) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    out.reserve(2 + nonce.size());
    out.push_back(COMMAND_CLASS);
    out.push_back(SECURITY_NONCE_REPORT);
    out.insert(out.end(), nonce.begin(), nonce.end());
    return out;
}

auto Security::decodeNonceReport(std::span<const std::uint8_t> payload) -> std::optional<S0::Nonce>
{
    constexpr std::size_t expected = 2 + S0::NONCE_SIZE;
    if (payload.size() != expected || payload[0] != COMMAND_CLASS || payload[1] != SECURITY_NONCE_REPORT)
    {
        return std::nullopt;
    }
    S0::Nonce nonce{};
    const auto body = payload.subspan(2, S0::NONCE_SIZE);
    std::copy(body.begin(), body.end(), nonce.begin());
    return nonce;
}

auto Security::commandByte(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>
{
    if (payload.size() < 2 || payload[0] != COMMAND_CLASS)
    {
        return std::nullopt;
    }
    return payload[1];
}
