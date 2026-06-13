#include "Kex.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace
{
constexpr std::size_t KEX_BODY_SIZE = 6;  // CC + cmd + props + schemes + curves + keys
constexpr std::size_t FAIL_SIZE     = 3;  // CC + cmd + fail type
constexpr std::size_t IDX_PROPS     = 2;
constexpr std::size_t IDX_SCHEMES   = 3;
constexpr std::size_t IDX_CURVES    = 4;
constexpr std::size_t IDX_KEYS      = 5;
}  // namespace

auto S2::Kex::encodeGet() -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, KEX_GET};
}

auto S2::Kex::encodeReport(const Report& report) -> std::vector<std::uint8_t>
{
    const auto props =
        static_cast<std::uint8_t>((report.echo ? PROP_ECHO : 0) | (report.requestCsa ? PROP_REQUEST_CSA : 0));
    return {COMMAND_CLASS, KEX_REPORT, props, report.supportedSchemes, report.supportedCurves, report.requestedKeys};
}

auto S2::Kex::encodeSet(const Set& set) -> std::vector<std::uint8_t>
{
    const auto props = static_cast<std::uint8_t>((set.echo ? PROP_ECHO : 0) | (set.requestCsa ? PROP_REQUEST_CSA : 0));
    return {COMMAND_CLASS, KEX_SET, props, set.selectedScheme, set.selectedCurve, set.grantedKeys};
}

auto S2::Kex::encodeFail(std::uint8_t failType) -> std::vector<std::uint8_t>
{
    return {COMMAND_CLASS, KEX_FAIL, failType};
}

auto S2::Kex::decodeReport(std::span<const std::uint8_t> payload) -> std::optional<Report>
{
    if (payload.size() < KEX_BODY_SIZE || payload[0] != COMMAND_CLASS || payload[1] != KEX_REPORT)
    {
        return std::nullopt;
    }
    return Report{
        .echo             = (payload[IDX_PROPS] & PROP_ECHO) != 0,
        .requestCsa       = (payload[IDX_PROPS] & PROP_REQUEST_CSA) != 0,
        .supportedSchemes = payload[IDX_SCHEMES],
        .supportedCurves  = payload[IDX_CURVES],
        .requestedKeys    = payload[IDX_KEYS],
    };
}

auto S2::Kex::decodeSet(std::span<const std::uint8_t> payload) -> std::optional<Set>
{
    if (payload.size() < KEX_BODY_SIZE || payload[0] != COMMAND_CLASS || payload[1] != KEX_SET)
    {
        return std::nullopt;
    }
    return Set{
        .echo           = (payload[IDX_PROPS] & PROP_ECHO) != 0,
        .requestCsa     = (payload[IDX_PROPS] & PROP_REQUEST_CSA) != 0,
        .selectedScheme = payload[IDX_SCHEMES],
        .selectedCurve  = payload[IDX_CURVES],
        .grantedKeys    = payload[IDX_KEYS],
    };
}

auto S2::Kex::decodeFail(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>
{
    if (payload.size() < FAIL_SIZE || payload[0] != COMMAND_CLASS || payload[1] != KEX_FAIL)
    {
        return std::nullopt;
    }
    return payload[IDX_PROPS];  // fail type sits where props would be
}

auto S2::Kex::commandByte(std::span<const std::uint8_t> payload) -> std::optional<std::uint8_t>
{
    if (payload.size() < 2 || payload[0] != COMMAND_CLASS)
    {
        return std::nullopt;
    }
    return payload[1];
}

auto S2::Kex::grantKeys(const Report& report, std::uint8_t controllerSupportedKeys) -> std::uint8_t
{
    std::uint8_t granted = report.requestedKeys & controllerSupportedKeys;
    if (report.requestCsa)
    {
        // CSA means the node has no DSK label, so it can't do DSK confirmation —
        // never grant the Access Control class to such a node.
        granted = static_cast<std::uint8_t>(granted & ~KEY_S2_ACCESS_CONTROL);
    }
    return granted;
}
