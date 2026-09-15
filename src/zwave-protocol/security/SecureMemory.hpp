#ifndef ZWAVED_SECURITY_SECURE_MEMORY_HPP
#define ZWAVED_SECURITY_SECURE_MEMORY_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

/// Secret-handling primitives shared by the S0 and S2 stacks (#238).
///
/// Two standard hygiene measures that key-handling code is expected to have:
///
/// **Constant-time comparison.** `std::equal` and `memcmp` short-circuit on the
/// first differing byte, so how long a MAC check takes leaks how many leading
/// bytes an attacker guessed right — the textbook shape of a forgery oracle.
/// `constantTimeEquals` compares every byte regardless.
///
/// **Zeroization.** A key left in freed memory can be recovered from a core
/// dump, a swap file, or whatever allocates that block next. A plain
/// `std::fill` is not enough: the compiler is entitled to delete a write to
/// storage that is about to die. `secureZero` wraps `OPENSSL_cleanse`, which
/// exists precisely to be un-eliminable.
namespace Secrets
{
/// Overwrite `buffer` so the secret cannot be recovered from freed memory.
/// Unlike `std::fill`, this will not be optimised away.
auto secureZero(std::span<std::uint8_t> buffer) -> void;

/// Compare two buffers in time that depends on their length, not their
/// contents. Returns false immediately for a length mismatch — a length is not
/// secret, and comparing different-length buffers is a programming error, not
/// an attack signal.
[[nodiscard]] auto constantTimeEquals(std::span<const std::uint8_t> lhs, std::span<const std::uint8_t> rhs) -> bool;

/// A fixed-size byte array that cleanses itself on destruction.
///
/// Fixed-size on purpose: a `std::vector` that reallocates leaves a copy of the
/// old buffer behind that no destructor will ever reach, so key material must
/// not live in a growable container (#238's third acceptance point).
///
/// Copies and moves are allowed — key material legitimately gets passed around
/// — but every instance cleanses its own storage when it dies, so a copy going
/// out of scope scrubs itself too.
template <std::size_t N> class SecretBytes
{
  public:
    SecretBytes() = default;

    explicit SecretBytes(const std::array<std::uint8_t, N>& bytes)
        : bytes_(bytes)
    {
    }

    SecretBytes(const SecretBytes& other)                        = default;
    auto operator=(const SecretBytes& other) -> SecretBytes&     = default;
    SecretBytes(SecretBytes&& other) noexcept                    = default;
    auto operator=(SecretBytes&& other) noexcept -> SecretBytes& = default;

    ~SecretBytes()
    {
        secureZero(std::span<std::uint8_t>(bytes_));
    }

    [[nodiscard]] auto bytes() const -> const std::array<std::uint8_t, N>&
    {
        return bytes_;
    }

    [[nodiscard]] auto mutableBytes() -> std::array<std::uint8_t, N>&
    {
        return bytes_;
    }

    [[nodiscard]] auto span() const -> std::span<const std::uint8_t>
    {
        return std::span<const std::uint8_t>(bytes_);
    }

  private:
    std::array<std::uint8_t, N> bytes_{};
};
}  // namespace Secrets

#endif  // ZWAVED_SECURITY_SECURE_MEMORY_HPP
