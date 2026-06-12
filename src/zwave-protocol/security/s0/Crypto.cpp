#include "Crypto.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include <openssl/evp.h>
#include <openssl/types.h>

namespace
{
// S0 key-derivation passwords: the network key is ECB-encrypted with a block
// of all-0xAA to get the encryption key, all-0x55 to get the auth key.
constexpr std::uint8_t KEY_DERIVE_ENC  = 0xAA;
constexpr std::uint8_t KEY_DERIVE_AUTH = 0x55;

using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

auto makeCtx() -> CipherCtx
{
    CipherCtx ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (ctx == nullptr)
    {
        throw std::runtime_error("S0::Crypto: EVP_CIPHER_CTX_new failed");
    }
    return ctx;
}

// Encrypt `input` with `cipher` under key/IV, no padding, appending to `out`.
// Used for ECB (single block), OFB (stream), and CBC (for the MAC).
auto encryptNoPadding(const EVP_CIPHER* cipher,
                      const S0::Crypto::Key& key,
                      const std::uint8_t* initVector,
                      std::span<const std::uint8_t> input,
                      std::span<std::uint8_t> out) -> void
{
    auto ctx = makeCtx();
    if (EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, key.data(), initVector) != 1)
    {
        throw std::runtime_error("S0::Crypto: EVP_EncryptInit_ex failed");
    }
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);
    int written = 0;
    if (EVP_EncryptUpdate(ctx.get(), out.data(), &written, input.data(), static_cast<int>(input.size())) != 1)
    {
        throw std::runtime_error("S0::Crypto: EVP_EncryptUpdate failed");
    }
    int finalWritten = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), out.data() + written, &finalWritten) != 1)
    {
        throw std::runtime_error("S0::Crypto: EVP_EncryptFinal_ex failed");
    }
}
}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Key/Block alias the same array type but are distinct roles
auto S0::Crypto::ecbEncryptBlock(const Key& key, const Block& input) -> Block
{
    Block out{};
    encryptNoPadding(EVP_aes_128_ecb(), key, nullptr, input, out);
    return out;
}

auto S0::Crypto::deriveKeys(const Key& networkKey) -> DerivedKeys
{
    Block encSeed{};
    Block authSeed{};
    encSeed.fill(KEY_DERIVE_ENC);
    authSeed.fill(KEY_DERIVE_AUTH);
    const Block enc  = ecbEncryptBlock(networkKey, encSeed);
    const Block auth = ecbEncryptBlock(networkKey, authSeed);

    DerivedKeys keys{};
    std::copy(enc.begin(), enc.end(), keys.encryption.begin());
    std::copy(auth.begin(), auth.end(), keys.authentication.begin());
    return keys;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Key/Block alias the same array type but are distinct roles
auto S0::Crypto::ofbCrypt(const Key& key,
                          const Block& initVector,
                          std::span<const std::uint8_t> data) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out(data.size());
    if (!data.empty())
    {
        encryptNoPadding(EVP_aes_128_ofb(), key, initVector.data(), data, out);
    }
    return out;
}

auto S0::Crypto::cbcMac(const Key& key, std::span<const std::uint8_t> data) -> Mac
{
    // CBC-MAC = the last ciphertext block of a CBC encryption with IV=0 over the
    // zero-padded message; we keep only its leading MAC_SIZE bytes. An empty
    // message still MACs over one all-zero block.
    const std::size_t padded = (data.empty()) ? BLOCK_SIZE : ((data.size() + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
    std::vector<std::uint8_t> input(padded, 0);
    std::copy(data.begin(), data.end(), input.begin());

    std::vector<std::uint8_t> cipher(padded, 0);
    const Block zeroIv{};
    encryptNoPadding(EVP_aes_128_cbc(), key, zeroIv.data(), input, cipher);

    Mac mac{};
    const auto lastBlock = static_cast<std::ptrdiff_t>(padded - BLOCK_SIZE);
    std::copy(cipher.begin() + lastBlock, cipher.begin() + lastBlock + MAC_SIZE, mac.begin());
    return mac;
}
