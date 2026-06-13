#include "Crypto.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/types.h>

namespace
{
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using Pkey      = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using PkeyCtx   = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using MacPtr    = std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)>;
using MacCtx    = std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)>;

[[noreturn]] auto fail(const char* what) -> void
{
    throw std::runtime_error(std::string("S2::Crypto: ") + what);
}

auto toInt(std::size_t value) -> int
{
    return static_cast<int>(value);
}
}  // namespace

auto S2::Crypto::ccmEncrypt(const Key& key,
                            std::span<const std::uint8_t> nonce,
                            std::span<const std::uint8_t> aad,
                            std::span<const std::uint8_t> plaintext,
                            std::size_t tagLength) -> std::vector<std::uint8_t>
{
    const CipherCtx ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (ctx == nullptr)
    {
        fail("EVP_CIPHER_CTX_new");
    }
    int len = 0;
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_ccm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_SET_IVLEN, toInt(nonce.size()), nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_SET_TAG, toInt(tagLength), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1)
    {
        fail("CCM encrypt init");
    }
    // CCM must be told the total plaintext length up front, then the AAD.
    if (EVP_EncryptUpdate(ctx.get(), nullptr, &len, nullptr, toInt(plaintext.size())) != 1)
    {
        fail("CCM set length");
    }
    if (!aad.empty() && EVP_EncryptUpdate(ctx.get(), nullptr, &len, aad.data(), toInt(aad.size())) != 1)
    {
        fail("CCM aad");
    }

    std::vector<std::uint8_t> out(plaintext.size() + tagLength);
    if (!plaintext.empty() &&
        EVP_EncryptUpdate(ctx.get(), out.data(), &len, plaintext.data(), toInt(plaintext.size())) != 1)
    {
        fail("CCM encrypt");
    }
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), out.data() + len, &finalLen) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_GET_TAG, toInt(tagLength), out.data() + plaintext.size()) != 1)
    {
        fail("CCM tag");
    }
    return out;
}

auto S2::Crypto::ccmDecrypt(const Key& key,
                            std::span<const std::uint8_t> nonce,
                            std::span<const std::uint8_t> aad,
                            std::span<const std::uint8_t> ciphertextWithTag,
                            std::size_t tagLength) -> std::optional<std::vector<std::uint8_t>>
{
    if (ciphertextWithTag.size() < tagLength)
    {
        return std::nullopt;
    }
    const std::size_t cipherLen = ciphertextWithTag.size() - tagLength;
    const auto cipher           = ciphertextWithTag.subspan(0, cipherLen);
    const auto tag              = ciphertextWithTag.subspan(cipherLen, tagLength);

    std::array<std::uint8_t, CMAC_SIZE> tagBuf{};  // CCM tag is <= 16 bytes; mutable for the API
    std::copy(tag.begin(), tag.end(), tagBuf.begin());

    const CipherCtx ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (ctx == nullptr)
    {
        fail("EVP_CIPHER_CTX_new");
    }
    int len = 0;
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_ccm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_SET_IVLEN, toInt(nonce.size()), nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_CCM_SET_TAG, toInt(tagLength), tagBuf.data()) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), nonce.data()) != 1)
    {
        fail("CCM decrypt init");
    }
    if (EVP_DecryptUpdate(ctx.get(), nullptr, &len, nullptr, toInt(cipherLen)) != 1)
    {
        fail("CCM set length");
    }
    if (!aad.empty() && EVP_DecryptUpdate(ctx.get(), nullptr, &len, aad.data(), toInt(aad.size())) != 1)
    {
        fail("CCM aad");
    }
    std::vector<std::uint8_t> out(cipherLen);
    // For CCM the tag is verified here; a non-positive return means auth failure.
    if (EVP_DecryptUpdate(ctx.get(), out.data(), &len, cipher.data(), toInt(cipherLen)) <= 0)
    {
        return std::nullopt;
    }
    return out;
}

auto S2::Crypto::cmac(const Key& key, std::span<const std::uint8_t> data) -> Mac
{
    const MacPtr mac(EVP_MAC_fetch(nullptr, "CMAC", nullptr), &EVP_MAC_free);
    if (mac == nullptr)
    {
        fail("EVP_MAC_fetch CMAC");
    }
    const MacCtx ctx(EVP_MAC_CTX_new(mac.get()), &EVP_MAC_CTX_free);
    if (ctx == nullptr)
    {
        fail("EVP_MAC_CTX_new");
    }
    std::array<char, sizeof("AES-128-CBC")> cipherName{"AES-128-CBC"};  // CMAC is CBC-MAC-derived
    std::array<OSSL_PARAM, 2> params{OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_CIPHER, cipherName.data(), 0),
                                     OSSL_PARAM_construct_end()};
    if (EVP_MAC_init(ctx.get(), key.data(), key.size(), params.data()) != 1)
    {
        fail("EVP_MAC_init");
    }
    if (!data.empty() && EVP_MAC_update(ctx.get(), data.data(), data.size()) != 1)
    {
        fail("EVP_MAC_update");
    }
    Mac out{};
    std::size_t outLen = 0;
    if (EVP_MAC_final(ctx.get(), out.data(), &outLen, out.size()) != 1)
    {
        fail("EVP_MAC_final");
    }
    return out;
}

auto S2::Crypto::aesEcbEncrypt(const Key& key, const Block& input) -> Block
{
    const CipherCtx ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (ctx == nullptr)
    {
        fail("EVP_CIPHER_CTX_new");
    }
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key.data(), nullptr) != 1)
    {
        fail("ECB init");
    }
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);
    Block out{};
    int len = 0;
    if (EVP_EncryptUpdate(ctx.get(), out.data(), &len, input.data(), toInt(input.size())) != 1)
    {
        fail("ECB encrypt");
    }
    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), out.data() + len, &finalLen) != 1)
    {
        fail("ECB final");
    }
    return out;
}

auto S2::Crypto::generateKeyPair() -> KeyPair
{
    const PkeyCtx pctx(EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr), &EVP_PKEY_CTX_free);
    if (pctx == nullptr || EVP_PKEY_keygen_init(pctx.get()) != 1)
    {
        fail("X25519 keygen init");
    }
    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_keygen(pctx.get(), &raw) != 1)
    {
        fail("X25519 keygen");
    }
    const Pkey pkey(raw, &EVP_PKEY_free);

    KeyPair pair{};
    std::size_t len = pair.publicKey.size();
    if (EVP_PKEY_get_raw_public_key(pkey.get(), pair.publicKey.data(), &len) != 1)
    {
        fail("X25519 export public");
    }
    len = pair.privateKey.size();
    if (EVP_PKEY_get_raw_private_key(pkey.get(), pair.privateKey.data(), &len) != 1)
    {
        fail("X25519 export private");
    }
    return pair;
}

auto S2::Crypto::ecdh(const PrivateKey& ours, const PublicKey& theirs) -> std::optional<SharedSecret>
{
    const Pkey priv(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, ours.data(), ours.size()), &EVP_PKEY_free);
    const Pkey peer(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, theirs.data(), theirs.size()),
                    &EVP_PKEY_free);
    if (priv == nullptr || peer == nullptr)
    {
        return std::nullopt;
    }
    const PkeyCtx ctx(EVP_PKEY_CTX_new(priv.get(), nullptr), &EVP_PKEY_CTX_free);
    if (ctx == nullptr || EVP_PKEY_derive_init(ctx.get()) != 1 || EVP_PKEY_derive_set_peer(ctx.get(), peer.get()) != 1)
    {
        return std::nullopt;
    }
    SharedSecret secret{};
    std::size_t len = secret.size();
    if (EVP_PKEY_derive(ctx.get(), secret.data(), &len) != 1 || len != secret.size())
    {
        return std::nullopt;
    }
    return secret;
}
