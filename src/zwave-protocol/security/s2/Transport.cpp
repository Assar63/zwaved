#include "Transport.hpp"

#include "NetworkKeys.hpp"

#include <cstdint>
#include <optional>

namespace
{
// NodeRegistry::SecurityScheme numeric values for the S2 classes (kept in sync
// with the enum; this module stays free of the node-registry dependency).
constexpr std::uint8_t SCHEME_S2_UNAUTHENTICATED = 2;
constexpr std::uint8_t SCHEME_S2_AUTHENTICATED   = 3;
constexpr std::uint8_t SCHEME_S2_ACCESS_CONTROL  = 4;

auto classForScheme(std::uint8_t securityScheme) -> std::optional<S2::NetworkKeys::Class>
{
    switch (securityScheme)
    {
    case SCHEME_S2_UNAUTHENTICATED:
        return S2::NetworkKeys::Class::Unauthenticated;
    case SCHEME_S2_AUTHENTICATED:
        return S2::NetworkKeys::Class::Authenticated;
    case SCHEME_S2_ACCESS_CONTROL:
        return S2::NetworkKeys::Class::AccessControl;
    default:
        return std::nullopt;
    }
}
}  // namespace

auto S2::Transport::manager() -> SpanManager&
{
    static SpanManager instance;
    return instance;
}

auto S2::Transport::resolveClassKeys(std::uint8_t securityScheme) -> std::optional<KeyDerivation::NetworkKeys>
{
    const auto cls = classForScheme(securityScheme);
    if (!cls.has_value())
    {
        return std::nullopt;
    }
    const auto keys = NetworkKeys::current();
    if (!keys.has_value())
    {
        return std::nullopt;
    }
    return KeyDerivation::networkKeyExpand(NetworkKeys::keyFor(*keys, *cls));
}
