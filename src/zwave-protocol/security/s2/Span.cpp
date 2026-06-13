#include "Span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>

namespace
{
constexpr std::uint8_t CONST_NONCE_BYTE = 0x26;  // CKDF-MEI-Extract CMAC key, ×16
constexpr std::uint8_t CONST_EI_BYTE    = 0x88;  // CKDF-MEI-Expand constant, ×15
constexpr std::size_t CONST_EI_LEN      = 15;
constexpr std::size_t DRBG_SEED_LEN     = 2 * S2::Crypto::BLOCK_SIZE;  // AES-128 CTR_DRBG seedlen = 32

// CTR_DRBG_Update(provided_data[32], Key, V) — SP800-90A, no derivation function.
auto drbgUpdate(const S2::SPAN::InnerState& provided, S2::Crypto::Key& key, S2::Crypto::Block& value) -> void
{
    S2::SPAN::InnerState temp{};
    for (std::size_t block = 0; block < DRBG_SEED_LEN / S2::Crypto::BLOCK_SIZE; ++block)
    {
        S2::SPAN::incrementCounter(value);
        const S2::Crypto::Block out = S2::Crypto::aesEcbEncrypt(key, value);
        std::copy(out.begin(), out.end(), temp.begin() + (block * S2::Crypto::BLOCK_SIZE));
    }
    for (std::size_t i = 0; i < temp.size(); ++i)
    {
        temp.at(i) ^= provided.at(i);
    }
    std::copy(temp.begin(), temp.begin() + S2::Crypto::KEY_SIZE, key.begin());
    std::copy(temp.begin() + S2::Crypto::KEY_SIZE, temp.end(), value.begin());
}
}  // namespace

auto S2::SPAN::incrementCounter(Crypto::Block& counter) -> void
{
    for (std::size_t i = counter.size(); i-- > 0;)
    {
        if (++counter.at(i) != 0)
        {
            break;  // no carry out of this byte
        }
    }
}

auto S2::SPAN::mixEntropy(const EntropyInput& senderEI, const EntropyInput& receiverEI) -> Mei
{
    // CKDF-MEI-Extract: NoncePRK = CMAC(0x26·16, SenderEI ‖ ReceiverEI).
    Crypto::Key constNonce{};
    constNonce.fill(CONST_NONCE_BYTE);
    std::array<std::uint8_t, 2 * EI_SIZE> eis{};
    std::copy(senderEI.begin(), senderEI.end(), eis.begin());
    std::copy(receiverEI.begin(), receiverEI.end(), eis.begin() + EI_SIZE);
    const Crypto::Mac noncePrk = Crypto::cmac(constNonce, std::span<const std::uint8_t>(eis));

    Crypto::Key prkKey{};
    std::copy(noncePrk.begin(), noncePrk.end(), prkKey.begin());

    // CKDF-MEI-Expand. expandSeed (T0) = (0x88·15)‖0x00.
    std::array<std::uint8_t, Crypto::BLOCK_SIZE> expandSeed{};
    std::fill(expandSeed.begin(), expandSeed.begin() + CONST_EI_LEN, CONST_EI_BYTE);  // last byte stays 0x00

    // msg = prev(16) ‖ (0x88·15) ‖ counter. T1 chains from T0, T2 from T1.
    const auto expandBlock = [&](const std::array<std::uint8_t, Crypto::BLOCK_SIZE>& prev,
                                 std::uint8_t counter) -> Crypto::Mac
    {
        std::array<std::uint8_t, Crypto::BLOCK_SIZE + CONST_EI_LEN + 1> msg{};
        std::copy(prev.begin(), prev.end(), msg.begin());
        std::fill(msg.begin() + Crypto::BLOCK_SIZE, msg.end() - 1, CONST_EI_BYTE);
        msg.back() = counter;
        return Crypto::cmac(prkKey, std::span<const std::uint8_t>(msg));
    };

    const Crypto::Mac meiLow  = expandBlock(expandSeed, 0x01);  // T1
    const Crypto::Mac meiHigh = expandBlock(meiLow, 0x02);      // T2

    Mei mei{};
    std::copy(meiLow.begin(), meiLow.end(), mei.begin());
    std::copy(meiHigh.begin(), meiHigh.end(), mei.begin() + Crypto::BLOCK_SIZE);
    return mei;
}

auto S2::SPAN::Span::instantiate(const EntropyInput& senderEI,
                                 const EntropyInput& receiverEI,
                                 const Personalization& personalization) -> Span
{
    const Mei mei = mixEntropy(senderEI, receiverEI);
    // CTR_DRBG instantiate (no df): seed = entropy XOR personalization; Key=V=0; Update(seed).
    InnerState seed{};
    for (std::size_t i = 0; i < seed.size(); ++i)
    {
        seed.at(i) = mei.at(i) ^ personalization.at(i);
    }
    Span span;  // key_ / value_ default to all-zero
    drbgUpdate(seed, span.key_, span.value_);
    return span;
}

auto S2::SPAN::Span::nextNonce() -> Nonce
{
    // CTR_DRBG_Generate, 128 bits, empty additional_input: one block out, then Update(0).
    incrementCounter(value_);
    const Crypto::Block out = Crypto::aesEcbEncrypt(key_, value_);
    const InnerState zeros{};
    drbgUpdate(zeros, key_, value_);
    Nonce nonce{};
    std::copy(out.begin(), out.end(), nonce.begin());
    return nonce;
}

auto S2::SPAN::Span::serialize() const -> InnerState
{
    InnerState state{};
    std::copy(key_.begin(), key_.end(), state.begin());
    std::copy(value_.begin(), value_.end(), state.begin() + Crypto::KEY_SIZE);
    return state;
}

auto S2::SPAN::Span::deserialize(const InnerState& state) -> Span
{
    Span span;
    std::copy(state.begin(), state.begin() + Crypto::KEY_SIZE, span.key_.begin());
    std::copy(state.begin() + Crypto::KEY_SIZE, state.end(), span.value_.begin());
    return span;
}

auto S2::SPAN::Table::establish(std::uint8_t peer,
                                const EntropyInput& senderEI,
                                const EntropyInput& receiverEI,
                                const Personalization& personalization) -> void
{
    spans_.insert_or_assign(peer, Span::instantiate(senderEI, receiverEI, personalization));
}

auto S2::SPAN::Table::has(std::uint8_t peer) const -> bool
{
    return spans_.contains(peer);
}

auto S2::SPAN::Table::nextNonce(std::uint8_t peer) -> std::optional<Nonce>
{
    const auto iter = spans_.find(peer);
    if (iter == spans_.end())
    {
        return std::nullopt;
    }
    return iter->second.nextNonce();
}

auto S2::SPAN::Table::remove(std::uint8_t peer) -> void
{
    spans_.erase(peer);
}

auto S2::SPAN::table() -> Table&
{
    static Table instance;
    return instance;
}
