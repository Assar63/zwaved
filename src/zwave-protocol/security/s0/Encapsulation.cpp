#include "Encapsulation.hpp"

#include "Security.hpp"  // Security::COMMAND_CLASS, Security::SECURITY_MESSAGE_ENCAPSULATION

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
// Single, unsequenced frame — multi-segment sequencing is not used here.
constexpr std::uint8_t SEQUENCE_SINGLE = 0x00;
// Fixed frame overhead around the ciphertext: CC + cmd + IV(8) + nonceId(1) + MAC(8).
constexpr std::size_t HEADER_SIZE    = 2 + S0::NONCE_SIZE;          // [0x98][0x81] + senderNonce
constexpr std::size_t TRAILER_SIZE   = 1 + S0::Crypto::MAC_SIZE;    // receiverNonceId + MAC
constexpr std::size_t FRAME_OVERHEAD = HEADER_SIZE + TRAILER_SIZE;  // 19

// IV = senderNonce(8) ‖ receiverNonce(8).
auto makeIv(const S0::Nonce& senderNonce, const S0::Nonce& receiverNonce) -> S0::Crypto::Block
{
    S0::Crypto::Block ivBlock{};
    std::copy(senderNonce.begin(), senderNonce.end(), ivBlock.begin());
    std::copy(receiverNonce.begin(), receiverNonce.end(), ivBlock.begin() + S0::NONCE_SIZE);
    return ivBlock;
}

// Authentication data fed to CBC-MAC:
//   senderNonce ‖ receiverNonce ‖ [0x81, senderNodeId, receiverNodeId, len] ‖ ciphertext
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): assembled in fixed wire order, not called ad hoc
auto authData(const S0::Nonce& senderNonce,
              const S0::Nonce& receiverNonce,
              std::uint8_t senderNodeId,
              std::uint8_t receiverNodeId,
              std::span<const std::uint8_t> ciphertext) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> data;
    data.reserve((2 * S0::NONCE_SIZE) + 4 + ciphertext.size());
    data.insert(data.end(), senderNonce.begin(), senderNonce.end());
    data.insert(data.end(), receiverNonce.begin(), receiverNonce.end());
    data.push_back(Security::SECURITY_MESSAGE_ENCAPSULATION);
    data.push_back(senderNodeId);
    data.push_back(receiverNodeId);
    data.push_back(static_cast<std::uint8_t>(ciphertext.size()));
    data.insert(data.end(), ciphertext.begin(), ciphertext.end());
    return data;
}
}  // namespace

auto S0::Encapsulation::encrypt(std::span<const std::uint8_t> inner,
                                std::uint8_t senderNodeId,
                                std::uint8_t receiverNodeId,
                                const Nonce& senderNonce,
                                const Nonce& receiverNonce,
                                const Crypto::Key& networkKey) -> std::vector<std::uint8_t>
{
    const auto keys = Crypto::deriveKeys(networkKey);

    std::vector<std::uint8_t> plaintext;
    plaintext.reserve(1 + inner.size());
    plaintext.push_back(SEQUENCE_SINGLE);
    plaintext.insert(plaintext.end(), inner.begin(), inner.end());

    const auto ivBlock    = makeIv(senderNonce, receiverNonce);
    const auto ciphertext = Crypto::ofbCrypt(keys.encryption, ivBlock, plaintext);
    const auto mac        = Crypto::cbcMac(keys.authentication,
                                    authData(senderNonce, receiverNonce, senderNodeId, receiverNodeId, ciphertext));

    std::vector<std::uint8_t> frame;
    frame.reserve(FRAME_OVERHEAD + ciphertext.size());
    frame.push_back(Security::COMMAND_CLASS);
    frame.push_back(Security::SECURITY_MESSAGE_ENCAPSULATION);
    frame.insert(frame.end(), senderNonce.begin(), senderNonce.end());
    frame.insert(frame.end(), ciphertext.begin(), ciphertext.end());
    frame.push_back(receiverNonce[0]);  // receiver-nonce identifier
    frame.insert(frame.end(), mac.begin(), mac.end());
    return frame;
}

auto S0::Encapsulation::decrypt(std::span<const std::uint8_t> frame,
                                std::uint8_t senderNodeId,
                                std::uint8_t receiverNodeId,
                                const Nonce& ourNonce,
                                const Crypto::Key& networkKey) -> std::optional<std::vector<std::uint8_t>>
{
    // Need room for the overhead plus at least the 1-byte sequence prefix.
    if (frame.size() < FRAME_OVERHEAD + 1 || frame[0] != Security::COMMAND_CLASS ||
        frame[1] != Security::SECURITY_MESSAGE_ENCAPSULATION)
    {
        return std::nullopt;
    }

    Nonce senderNonce{};
    const auto ivField = frame.subspan(2, NONCE_SIZE);
    std::copy(ivField.begin(), ivField.end(), senderNonce.begin());

    const std::size_t cipherLen = frame.size() - FRAME_OVERHEAD;
    const auto ciphertext       = frame.subspan(HEADER_SIZE, cipherLen);
    const std::uint8_t nonceId  = frame[HEADER_SIZE + cipherLen];
    const auto mac              = frame.subspan(HEADER_SIZE + cipherLen + 1, Crypto::MAC_SIZE);

    if (nonceId != ourNonce[0])
    {
        return std::nullopt;  // not the nonce we issued (or it has been recycled)
    }

    const auto keys = Crypto::deriveKeys(networkKey);
    const auto expected =
        Crypto::cbcMac(keys.authentication, authData(senderNonce, ourNonce, senderNodeId, receiverNodeId, ciphertext));
    if (!std::equal(expected.begin(), expected.end(), mac.begin()))
    {
        return std::nullopt;  // tampered, or wrong/stale nonce — drop silently
    }

    const auto ivBlock   = makeIv(senderNonce, ourNonce);
    const auto plaintext = Crypto::ofbCrypt(keys.encryption, ivBlock, ciphertext);
    if (plaintext.empty())
    {
        return std::nullopt;
    }
    // Drop the leading sequence byte; the rest is the inner CC frame.
    return std::vector<std::uint8_t>(plaintext.begin() + 1, plaintext.end());
}
