#include "KeyInstall.hpp"

#include "Kex.hpp"  // Kex::KEY_* class bits (single source of Table 4.19)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
constexpr std::size_t GET_SIZE          = 3;                         // CC + cmd + requested key
constexpr std::size_t REPORT_SIZE       = 3 + S2::Crypto::KEY_SIZE;  // CC + cmd + granted key + 16-byte key
constexpr std::size_t TRANSFER_END_SIZE = 3;                         // CC + cmd + props
constexpr std::size_t IDX_FIELD         = 2;                         // requested/granted key, or props
constexpr std::size_t IDX_KEY           = 3;                         // network key bytes in a REPORT
}  // namespace

auto S2::KeyInstall::encodeKeyGet(std::uint8_t requestedKeyBit) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, NETWORK_KEY_GET, requestedKeyBit};
}

auto S2::KeyInstall::encodeKeyReport(std::uint8_t grantedKeyBit, const Crypto::Key& key) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> frame{COMMAND_CLASS, NETWORK_KEY_REPORT, grantedKeyBit};
    frame.insert(frame.end(), key.begin(), key.end());
    return frame;
}

auto S2::KeyInstall::encodeKeyVerify() -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, NETWORK_KEY_VERIFY};
}

auto S2::KeyInstall::encodeTransferEnd(bool keyVerified, bool keyRequestComplete) -> std::vector<std::uint8_t>
{
    const auto props = static_cast<std::uint8_t>((keyVerified ? TRANSFER_KEY_VERIFIED : 0) |
                                                 (keyRequestComplete ? TRANSFER_KEY_REQUEST_COMPLETE : 0));
    return {COMMAND_CLASS, TRANSFER_END, props};
}

auto S2::KeyInstall::decodeKeyGet(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>
{
    if (payload.size() < GET_SIZE || payload[0] != COMMAND_CLASS || payload[1] != NETWORK_KEY_GET)
    {
        return std::nullopt;
    }
    return payload[IDX_FIELD];
}

auto S2::KeyInstall::decodeKeyReport(std::span<const std::uint8_t> payload) -> std::optional<KeyReport>
{
    if (payload.size() != REPORT_SIZE || payload[0] != COMMAND_CLASS || payload[1] != NETWORK_KEY_REPORT)
    {
        return std::nullopt;
    }
    KeyReport report{.grantedKey = payload[IDX_FIELD], .key = {}};
    const auto keyBytes = payload.subspan(IDX_KEY, Crypto::KEY_SIZE);
    std::copy(keyBytes.begin(), keyBytes.end(), report.key.begin());
    return report;
}

auto S2::KeyInstall::decodeTransferEnd(std::span<const std::uint8_t> payload) -> std::optional<TransferEnd>
{
    if (payload.size() < TRANSFER_END_SIZE || payload[0] != COMMAND_CLASS || payload[1] != TRANSFER_END)
    {
        return std::nullopt;
    }
    return TransferEnd{
        .keyVerified        = (payload[IDX_FIELD] & TRANSFER_KEY_VERIFIED) != 0,
        .keyRequestComplete = (payload[IDX_FIELD] & TRANSFER_KEY_REQUEST_COMPLETE) != 0,
    };
}

auto S2::KeyInstall::commandByte(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>
{
    if (payload.size() < 2 || payload[0] != COMMAND_CLASS)
    {
        return std::nullopt;
    }
    return payload[1];
}

auto S2::KeyInstall::classForKeyBit(std::uint8_t keyBit) -> std::optional<NetworkKeys::Class>
{
    switch (keyBit)
    {
    case Kex::KEY_S2_UNAUTHENTICATED:
        return NetworkKeys::Class::Unauthenticated;
    case Kex::KEY_S2_AUTHENTICATED:
        return NetworkKeys::Class::Authenticated;
    case Kex::KEY_S2_ACCESS_CONTROL:
        return NetworkKeys::Class::AccessControl;
    case Kex::KEY_S0:
        return NetworkKeys::Class::S0Compat;
    default:
        return std::nullopt;
    }
}
