#include "KeyDerivation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

namespace
{
constexpr std::uint8_t CONST_PRK_BYTE  = 0x33;  // CKDF-TempExtract CMAC key, ×16
constexpr std::size_t CONST_PRK_LEN    = 16;
constexpr std::uint8_t CONST_TE_BYTE   = 0x88;  // CKDF-TempExpand constant, ×15
constexpr std::uint8_t CONST_NK_BYTE   = 0x55;  // CKDF-NetworkKeyExpand constant, ×15
constexpr std::size_t CONST_EXPAND_LEN = 15;

// Generic CKDF expand: T1 = CMAC(prk, const|0x01), Ti = CMAC(prk, T(i-1)|const|i),
// for i in 1..blocks. Returns T1..Tblocks.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): const byte vs block count are distinct roles
auto ckdfExpand(const S2::Crypto::Key& prk, std::uint8_t constByte, int blocks) -> std::vector<S2::Crypto::Mac>
{
    std::vector<S2::Crypto::Mac> chain;
    chain.reserve(static_cast<std::size_t>(blocks));
    for (int index = 1; index <= blocks; ++index)
    {
        std::vector<std::uint8_t> msg;
        if (index > 1)
        {
            const auto& prev = chain.back();
            msg.insert(msg.end(), prev.begin(), prev.end());
        }
        msg.insert(msg.end(), CONST_EXPAND_LEN, constByte);
        msg.push_back(static_cast<std::uint8_t>(index));
        chain.push_back(S2::Crypto::cmac(prk, std::span<const std::uint8_t>(msg)));
    }
    return chain;
}

// Concatenate two 16-byte MACs into a 32-byte personalization string.
auto join(const S2::Crypto::Mac& low, const S2::Crypto::Mac& high) -> S2::KeyDerivation::Personalization
{
    S2::KeyDerivation::Personalization out{};
    std::copy(low.begin(), low.end(), out.begin());
    std::copy(high.begin(), high.end(), out.begin() + low.size());
    return out;
}
}  // namespace

auto S2::KeyDerivation::tempExtract(const Crypto::SharedSecret& sharedSecret,
                                    const Crypto::PublicKey& controllerPublicKey,
                                    const Crypto::PublicKey& nodePublicKey) -> Crypto::Key
{
    Crypto::Key constPrk{};
    constPrk.fill(CONST_PRK_BYTE);

    std::vector<std::uint8_t> message;
    message.reserve(sharedSecret.size() + controllerPublicKey.size() + nodePublicKey.size());
    message.insert(message.end(), sharedSecret.begin(), sharedSecret.end());
    message.insert(message.end(), controllerPublicKey.begin(), controllerPublicKey.end());
    message.insert(message.end(), nodePublicKey.begin(), nodePublicKey.end());

    static_assert(CONST_PRK_LEN == std::tuple_size_v<Crypto::Key>);
    return Crypto::cmac(constPrk, std::span<const std::uint8_t>(message));
}

auto S2::KeyDerivation::tempExpand(const Crypto::Key& prk) -> TempKeys
{
    const auto chain = ckdfExpand(prk, CONST_TE_BYTE, 3);  // T1..T3
    return TempKeys{.keyCcm = chain[0], .personalization = join(chain[1], chain[2])};
}

auto S2::KeyDerivation::deriveTempKeys(const Crypto::SharedSecret& sharedSecret,
                                       const Crypto::PublicKey& controllerPublicKey,
                                       const Crypto::PublicKey& nodePublicKey) -> TempKeys
{
    return tempExpand(tempExtract(sharedSecret, controllerPublicKey, nodePublicKey));
}

auto S2::KeyDerivation::networkKeyExpand(const Crypto::Key& networkKey) -> NetworkKeys
{
    const auto chain = ckdfExpand(networkKey, CONST_NK_BYTE, 4);  // T1..T4
    return NetworkKeys{.keyCcm = chain[0], .personalization = join(chain[1], chain[2]), .keyMpan = chain[3]};
}
