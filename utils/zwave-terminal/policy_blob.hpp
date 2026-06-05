#ifndef ZWAVE_TERMINAL_POLICY_BLOB_HPP
#define ZWAVE_TERMINAL_POLICY_BLOB_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Policy BLOB codec for the zwave-terminal client. Reimplements the daemon's
// PolicyRegister length-prefixed wire format (see
// src/policy-register/PolicyRegister.cpp) so the terminal stays a standalone
// D-Bus client that doesn't link daemon code — kept deliberately duplicated
// (see #111). The authoritative format lives in the daemon; the leading
// version byte makes a drift detectable rather than silently misparsed.
namespace zwt
{
// A decoded policy entry. Tagged by `kind`; only the matching fields are
// meaningful. Mirrors PolicyRegister::PolicyEntry without pulling in the
// daemon's variant type.
struct PolicyEntry
{
    std::uint8_t kind = 0;
    // POLICY_KIND_CONFIG
    std::uint8_t parameter = 0;
    std::uint8_t size      = 1;
    bool isSigned          = false;
    std::int32_t value     = 0;
    // POLICY_KIND_ASSOC
    std::uint8_t groupId = 0;
    std::vector<std::uint8_t> members;
    // POLICY_KIND_WAKEUP
    std::uint32_t intervalSeconds   = 0;
    std::uint8_t notificationNodeId = 0;
};

[[nodiscard]] auto readU32Be(const std::vector<std::uint8_t>& bytes, std::size_t& pos) -> std::uint32_t;
auto appendU32Be(std::vector<std::uint8_t>& out, std::uint32_t value) -> void;
[[nodiscard]] auto decodePolicy(const std::vector<std::uint8_t>& bytes) -> std::optional<std::vector<PolicyEntry>>;
[[nodiscard]] auto encodePolicy(const std::vector<PolicyEntry>& policy) -> std::vector<std::uint8_t>;
auto logPolicy(const std::string& header, const std::vector<std::uint8_t>& bytes) -> void;
[[nodiscard]] auto promptPolicyEntry() -> std::optional<PolicyEntry>;
[[nodiscard]] auto sameSlot(const PolicyEntry& lhs, const PolicyEntry& rhs) -> bool;
[[nodiscard]] auto applyEntryToBlob(const std::vector<std::uint8_t>& existing,
                                    const PolicyEntry& entry,
                                    bool& replaced) -> std::optional<std::vector<std::uint8_t>>;
[[nodiscard]] auto entrySummary(const PolicyEntry& entry) -> std::string;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_POLICY_BLOB_HPP
