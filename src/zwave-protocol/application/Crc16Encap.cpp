#include "Crc16Encap.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
// CRC-16/AUG-CCITT parameters.
constexpr std::uint16_t CRC_INIT = 0x1D0F;
constexpr std::uint16_t CRC_POLY = 0x1021;
constexpr int BITS_PER_BYTE      = 8;
constexpr std::uint16_t MSB_MASK = 0x8000;

// CC + cmd + at least one inner byte + 2-byte CRC trailer.
constexpr std::size_t MIN_BYTES   = 5;
constexpr std::size_t TRAILER_LEN = 2;
constexpr std::size_t INNER_START = 2;
constexpr int BYTE_SHIFT          = 8;
constexpr std::uint16_t BYTE_MASK = 0x00FF;
}  // namespace

auto Crc16Encap::checksum(std::span<const std::uint8_t> data) -> std::uint16_t
{
    std::uint16_t crc = CRC_INIT;
    for (const auto byte : data)
    {
        crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(byte) << BITS_PER_BYTE));
        for (int bit = 0; bit < BITS_PER_BYTE; ++bit)
        {
            if ((crc & MSB_MASK) != 0)
            {
                crc = static_cast<std::uint16_t>((crc << 1) ^ CRC_POLY);
            }
            else
            {
                crc = static_cast<std::uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

auto Crc16Encap::verifyAndUnwrap(std::span<const std::uint8_t> payload) -> std::optional<std::vector<std::uint8_t>>
{
    if (payload.size() < MIN_BYTES || payload[0] != COMMAND_CLASS || payload[1] != CRC16_ENCAP)
    {
        return std::nullopt;
    }
    const std::size_t bodyLen = payload.size() - TRAILER_LEN;  // everything the CRC covers
    const auto expected       = checksum(payload.subspan(0, bodyLen));
    const auto actual = static_cast<std::uint16_t>((static_cast<std::uint16_t>(payload[bodyLen]) << BYTE_SHIFT) |
                                                   (payload[bodyLen + 1] & BYTE_MASK));
    if (expected != actual)
    {
        return std::nullopt;  // corrupt frame — drop it
    }
    const auto inner = payload.subspan(INNER_START, bodyLen - INNER_START);
    return std::vector<std::uint8_t>(inner.begin(), inner.end());
}

auto Crc16Encap::encode(std::span<const std::uint8_t> innerCommand) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    out.reserve(INNER_START + innerCommand.size() + TRAILER_LEN);
    out.push_back(COMMAND_CLASS);
    out.push_back(CRC16_ENCAP);
    out.insert(out.end(), innerCommand.begin(), innerCommand.end());
    const auto crc = checksum(out);
    out.push_back(static_cast<std::uint8_t>((crc >> BYTE_SHIFT) & BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>(crc & BYTE_MASK));
    return out;
}
