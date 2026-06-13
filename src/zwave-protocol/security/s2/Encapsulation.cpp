#include "Encapsulation.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
constexpr std::uint8_t CC_SECURITY_2      = 0x9F;
constexpr std::uint8_t CMD_ENCAP          = 0x03;
constexpr std::uint8_t PROP_EXT           = 0x01;  // non-encrypted extensions present
constexpr std::uint8_t PROP_ENC_EXT       = 0x02;  // encrypted extensions present (inside ciphertext)
constexpr std::uint8_t EXT_MORE_TO_FOLLOW = 0x80;
constexpr std::size_t HEADER_SIZE         = 4;  // CC + cmd + seq + props
constexpr std::size_t EXT_MIN_BYTES       = 2;  // length byte + flags/type byte
constexpr int BYTE_SHIFT                  = 8;
constexpr std::uint16_t BYTE_MASK         = 0x00FF;

// Total byte length of the chain of extension objects starting at `data[0]`,
// or std::nullopt if malformed. Each object: [Length][flags|type][data…], with
// Length covering the whole object and bit7 of the flags byte = More-to-Follow.
auto extensionsLength(std::span<const std::uint8_t> data) -> std::optional<std::size_t>
{
    std::size_t offset = 0;
    while (true)
    {
        if (offset + EXT_MIN_BYTES > data.size())
        {
            return std::nullopt;
        }
        const std::size_t extLen = data[offset];
        if (extLen < EXT_MIN_BYTES || offset + extLen > data.size())
        {
            return std::nullopt;
        }
        const bool moreToFollow = (data[offset + 1] & EXT_MORE_TO_FOLLOW) != 0;
        offset += extLen;
        if (!moreToFollow)
        {
            return offset;
        }
    }
}

// AAD per SDS13783 §4.2.6.4.6 (singlecast, node ids <= 255).
auto buildAad(const S2::Encapsulation::Context& ctx,
              std::uint8_t sequenceNumber,
              std::uint8_t props,
              std::span<const std::uint8_t> extensions,
              std::size_t frameLength) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> aad;
    aad.reserve(1 + 1 + ctx.homeId.size() + 2 + 1 + 1 + extensions.size());
    aad.push_back(ctx.senderNodeId);
    aad.push_back(ctx.receiverNodeId);
    aad.insert(aad.end(), ctx.homeId.begin(), ctx.homeId.end());
    aad.push_back(static_cast<std::uint8_t>((frameLength >> BYTE_SHIFT) & BYTE_MASK));
    aad.push_back(static_cast<std::uint8_t>(frameLength & BYTE_MASK));
    aad.push_back(sequenceNumber);
    aad.push_back(props);
    aad.insert(aad.end(), extensions.begin(), extensions.end());
    return aad;
}
}  // namespace

auto S2::Encapsulation::encrypt(std::span<const std::uint8_t> inner,
                                const Context& context,
                                const Crypto::Key& classKey,
                                const CcmNonce& nonce,
                                std::span<const std::uint8_t> nonEncryptedExtensions) -> std::vector<std::uint8_t>
{
    const std::uint8_t props = nonEncryptedExtensions.empty() ? 0x00 : PROP_EXT;
    const std::size_t frameLength =
        HEADER_SIZE + nonEncryptedExtensions.size() + inner.size() + TAG_SIZE;  // no encrypted extensions

    const auto aad        = buildAad(context, context.sequenceNumber, props, nonEncryptedExtensions, frameLength);
    const auto ciphertext = Crypto::ccmEncrypt(
        classKey, std::span<const std::uint8_t>(nonce), std::span<const std::uint8_t>(aad), inner, TAG_SIZE);

    std::vector<std::uint8_t> frame;
    frame.reserve(frameLength);
    frame.push_back(CC_SECURITY_2);
    frame.push_back(CMD_ENCAP);
    frame.push_back(context.sequenceNumber);
    frame.push_back(props);
    frame.insert(frame.end(), nonEncryptedExtensions.begin(), nonEncryptedExtensions.end());
    frame.insert(frame.end(), ciphertext.begin(), ciphertext.end());
    return frame;
}

auto S2::Encapsulation::decrypt(std::span<const std::uint8_t> frame,
                                const Context& context,
                                const Crypto::Key& classKey,
                                const CcmNonce& nonce) -> std::optional<std::vector<std::uint8_t>>
{
    if (frame.size() < HEADER_SIZE + TAG_SIZE || frame[0] != CC_SECURITY_2 || frame[1] != CMD_ENCAP)
    {
        return std::nullopt;
    }
    const std::uint8_t sequenceNumber = frame[2];
    const std::uint8_t props          = frame[3];

    std::size_t extBytes = 0;
    if ((props & PROP_EXT) != 0)
    {
        const auto length = extensionsLength(frame.subspan(HEADER_SIZE));
        if (!length.has_value())
        {
            return std::nullopt;
        }
        extBytes = *length;
    }
    const std::size_t cipherStart = HEADER_SIZE + extBytes;
    if (cipherStart + TAG_SIZE > frame.size())
    {
        return std::nullopt;
    }

    const auto aad = buildAad(context, sequenceNumber, props, frame.subspan(HEADER_SIZE, extBytes), frame.size());
    auto plaintext = Crypto::ccmDecrypt(classKey,
                                        std::span<const std::uint8_t>(nonce),
                                        std::span<const std::uint8_t>(aad),
                                        frame.subspan(cipherStart),
                                        TAG_SIZE);
    if (!plaintext.has_value())
    {
        return std::nullopt;  // wrong key / nonce / identity, or tampered
    }

    // Strip any encrypted extensions from the front of the plaintext.
    if ((props & PROP_ENC_EXT) != 0)
    {
        const auto length = extensionsLength(std::span<const std::uint8_t>(*plaintext));
        if (!length.has_value() || *length > plaintext->size())
        {
            return std::nullopt;
        }
        plaintext->erase(plaintext->begin(), plaintext->begin() + static_cast<std::ptrdiff_t>(*length));
    }
    return plaintext;
}
