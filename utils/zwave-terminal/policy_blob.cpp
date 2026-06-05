#include "policy_blob.hpp"

#include "activity.hpp"
#include "constants.hpp"
#include "prompts.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace zwt
{
auto readU32Be(const std::vector<std::uint8_t>& bytes, std::size_t& pos) -> std::uint32_t
{
    const std::uint32_t value = (static_cast<std::uint32_t>(bytes[pos]) << (3 * BITS_PER_BYTE)) |
                                (static_cast<std::uint32_t>(bytes[pos + 1]) << (2 * BITS_PER_BYTE)) |
                                (static_cast<std::uint32_t>(bytes[pos + 2]) << BITS_PER_BYTE) |
                                static_cast<std::uint32_t>(bytes[pos + 3]);
    pos += 4;
    return value;
}

auto appendU32Be(std::vector<std::uint8_t>& out, std::uint32_t value) -> void
{
    out.push_back(static_cast<std::uint8_t>((value >> (3 * BITS_PER_BYTE)) & U32_BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>((value >> (2 * BITS_PER_BYTE)) & U32_BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>((value >> BITS_PER_BYTE) & U32_BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>(value & U32_BYTE_MASK));
}

auto decodePolicy(const std::vector<std::uint8_t>& bytes) -> std::optional<std::vector<PolicyEntry>>
{
    std::size_t pos = 0;
    if (bytes.size() < 2 || bytes[pos++] != POLICY_BLOB_VERSION)
    {
        return std::nullopt;
    }
    const std::uint8_t count = bytes[pos++];
    std::vector<PolicyEntry> out;
    for (std::uint8_t idx = 0; idx < count; ++idx)
    {
        if (pos >= bytes.size())
        {
            return std::nullopt;
        }
        PolicyEntry entry;
        entry.kind = bytes[pos++];
        if (entry.kind == POLICY_KIND_CONFIG)
        {
            if (pos + POLICY_CONFIG_BODY_LEN > bytes.size())
            {
                return std::nullopt;
            }
            entry.parameter = bytes[pos++];
            entry.size      = bytes[pos++];
            entry.isSigned  = bytes[pos++] != 0;
            entry.value     = static_cast<std::int32_t>(readU32Be(bytes, pos));
        }
        else if (entry.kind == POLICY_KIND_ASSOC)
        {
            if (pos + POLICY_ASSOC_HEADER_LEN > bytes.size())
            {
                return std::nullopt;
            }
            entry.groupId               = bytes[pos++];
            const std::uint8_t memberCt = bytes[pos++];
            if (pos + memberCt > bytes.size())
            {
                return std::nullopt;
            }
            for (std::uint8_t member = 0; member < memberCt; ++member)
            {
                entry.members.push_back(bytes[pos++]);
            }
        }
        else if (entry.kind == POLICY_KIND_WAKEUP)
        {
            if (pos + POLICY_WAKEUP_BODY_LEN > bytes.size())
            {
                return std::nullopt;
            }
            entry.intervalSeconds    = readU32Be(bytes, pos);
            entry.notificationNodeId = bytes[pos++];
        }
        else
        {
            return std::nullopt;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

auto encodePolicy(const std::vector<PolicyEntry>& policy) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    out.push_back(POLICY_BLOB_VERSION);
    out.push_back(static_cast<std::uint8_t>(policy.size()));
    for (const auto& entry : policy)
    {
        out.push_back(entry.kind);
        if (entry.kind == POLICY_KIND_CONFIG)
        {
            out.push_back(entry.parameter);
            out.push_back(entry.size);
            out.push_back(entry.isSigned ? 1 : 0);
            appendU32Be(out, static_cast<std::uint32_t>(entry.value));
        }
        else if (entry.kind == POLICY_KIND_ASSOC)
        {
            out.push_back(entry.groupId);
            out.push_back(static_cast<std::uint8_t>(entry.members.size()));
            for (const auto member : entry.members)
            {
                out.push_back(member);
            }
        }
        else if (entry.kind == POLICY_KIND_WAKEUP)
        {
            appendU32Be(out, entry.intervalSeconds);
            out.push_back(entry.notificationNodeId);
        }
    }
    return out;
}

// Log a decoded policy under `header`. Empty policy logs "(empty)";
// a blob that fails to decode logs a hex dump so it's still visible.
auto logPolicy(const std::string& header, const std::vector<std::uint8_t>& bytes) -> void
{
    logLine(header);
    if (bytes.empty())
    {
        logLine("    (none)");
        return;
    }
    const auto policy = decodePolicy(bytes);
    if (!policy.has_value())
    {
        logLine("    (undecodable blob, " + std::to_string(bytes.size()) + " bytes)");
        return;
    }
    if (policy->empty())
    {
        logLine("    (empty)");
        return;
    }
    for (const auto& entry : *policy)
    {
        std::ostringstream stream;
        if (entry.kind == POLICY_KIND_CONFIG)
        {
            stream << "    config param=" << static_cast<unsigned>(entry.parameter)
                   << " size=" << static_cast<unsigned>(entry.size) << " value=" << entry.value
                   << (entry.isSigned ? " (signed)" : "");
        }
        else if (entry.kind == POLICY_KIND_ASSOC)
        {
            stream << "    assoc group=" << static_cast<unsigned>(entry.groupId) << " members=[";
            bool first = true;
            for (const auto member : entry.members)
            {
                if (!first)
                {
                    stream << " ";
                }
                first = false;
                stream << static_cast<unsigned>(member);
            }
            stream << "]";
        }
        else if (entry.kind == POLICY_KIND_WAKEUP)
        {
            stream << "    wakeup interval=" << entry.intervalSeconds
                   << "s notify=" << static_cast<unsigned>(entry.notificationNodeId);
        }
        logLine(stream.str());
    }
}


auto promptPolicyEntry() -> std::optional<PolicyEntry>
{
    auto kind = promptChar("Entry kind: [c]onfiguration  [a]ssociation  [w]ake-up:", "caw");
    if (!kind.has_value())
    {
        logLine("policy entry: cancelled");
        return std::nullopt;
    }
    if (*kind == 'c')
    {
        auto parameter = promptByte("Config parameter (0-255):", BYTE_MIN, BYTE_MAX);
        auto size      = promptByte("Value size bytes (1, 2, or 4):", CONFIG_SIZE_MIN, CONFIG_SIZE_MAX);
        auto value     = promptInt32("Value (signed int32):");
        if (!parameter.has_value() || !size.has_value() || !value.has_value() ||
            (*size != 1 && *size != 2 && *size != 4))
        {
            logLine("config entry: cancelled or invalid (size must be 1/2/4)");
            return std::nullopt;
        }
        return PolicyEntry{.kind      = POLICY_KIND_CONFIG,
                           .parameter = *parameter,
                           .size      = *size,
                           .isSigned  = *value < 0,
                           .value     = *value};
    }
    if (*kind == 'a')
    {
        auto group   = promptByte("Group id (1-255):", GROUP_ID_MIN, GROUP_ID_MAX);
        auto members = promptNodeList("Member node ids (space/comma separated):");
        if (!group.has_value() || !members.has_value())
        {
            logLine("association entry: cancelled or invalid");
            return std::nullopt;
        }
        return PolicyEntry{.kind = POLICY_KIND_ASSOC, .groupId = *group, .members = *members};
    }
    // wake-up
    auto interval = promptU32("Interval seconds (0..16777215):");
    auto notify   = promptByte("Notify node id (0=controller):", BYTE_MIN, BYTE_MAX);
    if (!interval.has_value() || !notify.has_value())
    {
        logLine("wake-up entry: cancelled or invalid");
        return std::nullopt;
    }
    return PolicyEntry{.kind = POLICY_KIND_WAKEUP, .intervalSeconds = *interval, .notificationNodeId = *notify};
}

// True iff two entries occupy the same policy slot — an override of this
// identity replaces rather than appends (Configuration keyed by
// parameter, Association by groupId, Wake-Up a singleton).
auto sameSlot(const PolicyEntry& lhs, const PolicyEntry& rhs) -> bool
{
    if (lhs.kind != rhs.kind)
    {
        return false;
    }
    if (lhs.kind == POLICY_KIND_CONFIG)
    {
        return lhs.parameter == rhs.parameter;
    }
    if (lhs.kind == POLICY_KIND_ASSOC)
    {
        return lhs.groupId == rhs.groupId;
    }
    return true;  // wake-up singleton
}

// Decode the existing blob, upsert `entry` (replace same-slot or append),
// and return the re-encoded blob. nullopt if the existing blob is
// non-empty but undecodable — don't clobber data we can't read. `replaced`
// reports whether an existing entry was overwritten.
auto applyEntryToBlob(const std::vector<std::uint8_t>& existing,
                      const PolicyEntry& entry,
                      bool& replaced) -> std::optional<std::vector<std::uint8_t>>
{
    std::vector<PolicyEntry> policy;
    if (!existing.empty())
    {
        auto decoded = decodePolicy(existing);
        if (!decoded.has_value())
        {
            return std::nullopt;
        }
        policy = *decoded;
    }
    replaced = false;
    for (auto& existingEntry : policy)
    {
        if (sameSlot(existingEntry, entry))
        {
            existingEntry = entry;
            replaced      = true;
            break;
        }
    }
    if (!replaced)
    {
        policy.push_back(entry);
    }
    return encodePolicy(policy);
}

// One-line summary of an entry for log messages.
auto entrySummary(const PolicyEntry& entry) -> std::string
{
    std::ostringstream stream;
    if (entry.kind == POLICY_KIND_CONFIG)
    {
        stream << "config param=" << static_cast<unsigned>(entry.parameter) << " value=" << entry.value;
    }
    else if (entry.kind == POLICY_KIND_ASSOC)
    {
        stream << "assoc group=" << static_cast<unsigned>(entry.groupId) << " members=" << entry.members.size();
    }
    else
    {
        stream << "wakeup interval=" << entry.intervalSeconds << "s";
    }
    return stream.str();
}

// Add or update a policy entry (Configuration / Association / Wake-Up) in
// a node's override, preserving any other entries: read the current
// override, decode it, upsert the entry, re-encode, and write it back.
}  // namespace zwt
