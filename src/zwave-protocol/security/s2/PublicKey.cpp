#include "PublicKey.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
constexpr std::size_t HEADER_SIZE        = 3;  // CC + cmd + props
constexpr std::size_t IDX_PROPS          = 2;
constexpr std::size_t DSK_GROUPS         = 8;  // 8 groups of 5 digits = the first 16 bytes
constexpr std::size_t PIN_DIGITS         = 5;
constexpr int BYTE_SHIFT                 = 8;
constexpr std::uint32_t DECIMAL_BASE     = 10;
constexpr std::uint32_t UINT16_MAX_VALUE = 0xFFFF;
constexpr std::uint16_t LOW_BYTE_MASK    = 0xFF;

// Big-endian 16-bit value of the byte pair at `index*2`.
auto groupValue(const S2::Crypto::PublicKey& key, std::size_t index) -> std::uint16_t
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(key.at(index * 2)) << BYTE_SHIFT) |
                                      key.at((index * 2) + 1));
}
}  // namespace

auto S2::PublicKey::encode(bool includingNode,
                           const Crypto::PublicKey& key,
                           std::size_t obfuscateLeading) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> frame;
    frame.reserve(HEADER_SIZE + key.size());
    frame.push_back(COMMAND_CLASS);
    frame.push_back(PUBLIC_KEY_REPORT);
    frame.push_back(includingNode ? PROP_INCLUDING_NODE : 0x00);
    frame.insert(frame.end(), key.begin(), key.end());
    for (std::size_t i = 0; i < obfuscateLeading && i < key.size(); ++i)
    {
        frame.at(HEADER_SIZE + i) = 0x00;
    }
    return frame;
}

auto S2::PublicKey::decode(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() != HEADER_SIZE + Crypto::CURVE25519_KEY_SIZE || payload[0] != COMMAND_CLASS ||
        payload[1] != PUBLIC_KEY_REPORT)
    {
        return std::nullopt;
    }
    Report report{.includingNode = (payload[IDX_PROPS] & PROP_INCLUDING_NODE) != 0, .key = {}};
    const auto keyBytes = payload.subspan(HEADER_SIZE, Crypto::CURVE25519_KEY_SIZE);
    std::copy(keyBytes.begin(), keyBytes.end(), report.key.begin());
    return report;
}

auto S2::PublicKey::dskString(const Crypto::PublicKey& key) -> std::string
{
    std::string out;
    for (std::size_t group = 0; group < DSK_GROUPS; ++group)
    {
        if (group != 0)
        {
            out.push_back('-');
        }
        out += std::format("{:05}", groupValue(key, group));
    }
    return out;
}

auto S2::PublicKey::dskPin(const Crypto::PublicKey& key) -> std::string
{
    return std::format("{:05}", groupValue(key, 0));
}

auto S2::PublicKey::parsePin(const std::string& pin) -> std::optional<std::uint16_t>
{
    if (pin.size() != PIN_DIGITS)
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (const char digit : pin)
    {
        if (digit < '0' || digit > '9')
        {
            return std::nullopt;
        }
        value = (value * DECIMAL_BASE) + static_cast<std::uint32_t>(digit - '0');
    }
    if (value > UINT16_MAX_VALUE)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

auto S2::PublicKey::applyPin(Crypto::PublicKey obfuscatedKey, std::uint16_t pin) -> Crypto::PublicKey
{
    obfuscatedKey.at(0) = static_cast<std::uint8_t>((pin >> BYTE_SHIFT) & LOW_BYTE_MASK);
    obfuscatedKey.at(1) = static_cast<std::uint8_t>(pin & LOW_BYTE_MASK);
    return obfuscatedKey;
}
