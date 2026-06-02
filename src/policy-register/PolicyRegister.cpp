#include "PolicyRegister.hpp"

#include "../logger/Logger.hpp"
#include "../message-bus/MessageBus.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <ios>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <sqlite3.h>

// ---- Policy serialization (free functions) --------------------------
//
// Length-prefixed binary, versioned so the on-disk form can evolve
// without a migration. Layout:
//   u8  version
//   u8  entryCount
//   entryCount × entry:
//     u8 kind
//     kind 1 Configuration: u8 parameter, u8 size, u8 signed, i32 value (BE)
//     kind 2 Association:    u8 groupId, u8 memberCount, memberCount × u8
//     kind 3 WakeUp:         u32 intervalSeconds (BE), u8 notificationNodeId

namespace
{
constexpr std::uint8_t POLICY_BLOB_VERSION = 1;
constexpr std::uint8_t KIND_CONFIGURATION  = 1;
constexpr std::uint8_t KIND_ASSOCIATION    = 2;
constexpr std::uint8_t KIND_WAKEUP         = 3;

constexpr unsigned BITS_PER_BYTE = 8;
constexpr unsigned BYTE_MASK     = 0xFF;

// Byte counts of the fixed parts of each entry body (after the kind
// byte) — used for bounds-checking during deserialize.
constexpr std::size_t CONFIG_BODY_LEN  = 7;  // parameter, size, signed, value(4)
constexpr std::size_t ASSOC_HEADER_LEN = 2;  // groupId, memberCount
constexpr std::size_t WAKEUP_BODY_LEN  = 5;  // intervalSeconds(4), notificationNodeId

auto appendU32Be(std::vector<std::uint8_t>& out, std::uint32_t value) -> void
{
    out.push_back(static_cast<std::uint8_t>((value >> (3 * BITS_PER_BYTE)) & BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>((value >> (2 * BITS_PER_BYTE)) & BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>((value >> BITS_PER_BYTE) & BYTE_MASK));
    out.push_back(static_cast<std::uint8_t>(value & BYTE_MASK));
}

// Read a big-endian u32 starting at `pos`, advancing it. Caller has
// already bounds-checked that 4 bytes are available.
auto readU32Be(const std::vector<std::uint8_t>& bytes, std::size_t& pos) -> std::uint32_t
{
    const std::uint32_t value = (static_cast<std::uint32_t>(bytes[pos]) << (3 * BITS_PER_BYTE)) |
                                (static_cast<std::uint32_t>(bytes[pos + 1]) << (2 * BITS_PER_BYTE)) |
                                (static_cast<std::uint32_t>(bytes[pos + 2]) << BITS_PER_BYTE) |
                                static_cast<std::uint32_t>(bytes[pos + 3]);
    pos += 4;
    return value;
}

// True iff `lhs` and `rhs` are the same policy *slot* — i.e. an override
// entry of this identity replaces (rather than appends to) the device
// default. Configuration is keyed by parameter, Association by groupId,
// Wake-Up is a singleton.
auto sameSlot(const PolicyRegister::PolicyEntry& lhs, const PolicyRegister::PolicyEntry& rhs) -> bool
{
    if (lhs.index() != rhs.index())
    {
        return false;
    }
    if (const auto* cfg = std::get_if<PolicyRegister::ConfigurationEntry>(&lhs))
    {
        return cfg->parameter == std::get<PolicyRegister::ConfigurationEntry>(rhs).parameter;
    }
    if (const auto* assoc = std::get_if<PolicyRegister::AssociationEntry>(&lhs))
    {
        return assoc->groupId == std::get<PolicyRegister::AssociationEntry>(rhs).groupId;
    }
    return true;  // WakeUp — single slot per policy
}
}  // namespace

auto PolicyRegister::serialize(const Policy& policy) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out;
    out.push_back(POLICY_BLOB_VERSION);
    out.push_back(static_cast<std::uint8_t>(policy.size()));
    for (const auto& entry : policy)
    {
        std::visit(
            [&out](const auto& concrete)
            {
                using T = std::decay_t<decltype(concrete)>;
                if constexpr (std::is_same_v<T, ConfigurationEntry>)
                {
                    out.push_back(KIND_CONFIGURATION);
                    out.push_back(concrete.parameter);
                    out.push_back(concrete.size);
                    out.push_back(concrete.isSigned ? 1 : 0);
                    appendU32Be(out, static_cast<std::uint32_t>(concrete.value));
                }
                else if constexpr (std::is_same_v<T, AssociationEntry>)
                {
                    out.push_back(KIND_ASSOCIATION);
                    out.push_back(concrete.groupId);
                    out.push_back(static_cast<std::uint8_t>(concrete.members.size()));
                    for (const auto member : concrete.members)
                    {
                        out.push_back(member);
                    }
                }
                else  // WakeUpEntry
                {
                    out.push_back(KIND_WAKEUP);
                    appendU32Be(out, concrete.intervalSeconds);
                    out.push_back(concrete.notificationNodeId);
                }
            },
            entry);
    }
    return out;
}

auto PolicyRegister::deserialize(const std::vector<std::uint8_t>& bytes) -> std::optional<Policy>
{
    std::size_t pos = 0;
    if (bytes.size() < 2 || bytes[pos++] != POLICY_BLOB_VERSION)
    {
        return std::nullopt;
    }
    const std::uint8_t count = bytes[pos++];
    Policy policy;
    for (std::uint8_t index = 0; index < count; ++index)
    {
        if (pos >= bytes.size())
        {
            return std::nullopt;
        }
        const std::uint8_t kind = bytes[pos++];
        if (kind == KIND_CONFIGURATION)
        {
            if (pos + CONFIG_BODY_LEN > bytes.size())
            {
                return std::nullopt;
            }
            ConfigurationEntry cfg;
            cfg.parameter = bytes[pos++];
            cfg.size      = bytes[pos++];
            cfg.isSigned  = bytes[pos++] != 0;
            cfg.value     = static_cast<std::int32_t>(readU32Be(bytes, pos));
            policy.emplace_back(cfg);
        }
        else if (kind == KIND_ASSOCIATION)
        {
            if (pos + ASSOC_HEADER_LEN > bytes.size())
            {
                return std::nullopt;
            }
            AssociationEntry assoc;
            assoc.groupId               = bytes[pos++];
            const std::uint8_t memberCt = bytes[pos++];
            if (pos + memberCt > bytes.size())
            {
                return std::nullopt;
            }
            for (std::uint8_t member = 0; member < memberCt; ++member)
            {
                assoc.members.push_back(bytes[pos++]);
            }
            policy.emplace_back(std::move(assoc));
        }
        else if (kind == KIND_WAKEUP)
        {
            if (pos + WAKEUP_BODY_LEN > bytes.size())
            {
                return std::nullopt;
            }
            WakeUpEntry wake;
            wake.intervalSeconds    = readU32Be(bytes, pos);
            wake.notificationNodeId = bytes[pos++];
            policy.emplace_back(wake);
        }
        else
        {
            return std::nullopt;
        }
    }
    return policy;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): distinct named roles
auto PolicyRegister::merge(const Policy& deviceDefault, const Policy& nodeOverride) -> Policy
{
    Policy result = deviceDefault;
    for (const auto& override : nodeOverride)
    {
        bool replaced = false;
        for (auto& existing : result)
        {
            if (sameSlot(existing, override))
            {
                existing = override;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            result.push_back(override);
        }
    }
    return result;
}

// ---- SQLite-backed register -----------------------------------------

namespace
{
constexpr const char* DEFAULT_STATE_DIR = "/var/lib/zwaved";
constexpr const char* STATE_DIR_ENV     = "ZWAVED_STATE_DIR";
constexpr const char* DB_FILENAME       = "nodes.db";

constexpr const char* SCHEMA_SQL = R"(
CREATE TABLE IF NOT EXISTS device_policies (
    manufacturer_id INTEGER NOT NULL,
    product_type_id INTEGER NOT NULL,
    product_id      INTEGER NOT NULL,
    policy          BLOB    NOT NULL,
    PRIMARY KEY (manufacturer_id, product_type_id, product_id)
);
CREATE TABLE IF NOT EXISTS node_policy_overrides (
    home_id TEXT    NOT NULL,
    node_id INTEGER NOT NULL,
    policy  BLOB    NOT NULL,
    PRIMARY KEY (home_id, node_id)
);
)";

constexpr const char* UPSERT_DEVICE_SQL =
    "INSERT INTO device_policies (manufacturer_id, product_type_id, product_id, policy) VALUES (?, ?, ?, ?) "
    "ON CONFLICT(manufacturer_id, product_type_id, product_id) DO UPDATE SET policy = excluded.policy";
constexpr const char* SELECT_DEVICE_SQL =
    "SELECT policy FROM device_policies WHERE manufacturer_id = ? AND product_type_id = ? AND product_id = ?";
constexpr const char* DELETE_DEVICE_SQL =
    "DELETE FROM device_policies WHERE manufacturer_id = ? AND product_type_id = ? AND product_id = ?";

constexpr const char* UPSERT_NODE_SQL = "INSERT INTO node_policy_overrides (home_id, node_id, policy) VALUES (?, ?, ?) "
                                        "ON CONFLICT(home_id, node_id) DO UPDATE SET policy = excluded.policy";
constexpr const char* SELECT_NODE_SQL = "SELECT policy FROM node_policy_overrides WHERE home_id = ? AND node_id = ?";
constexpr const char* DELETE_NODE_SQL = "DELETE FROM node_policy_overrides WHERE home_id = ? AND node_id = ?";

auto formatHomeId(const std::vector<std::uint8_t>& bytes) -> std::string
{
    std::ostringstream stream;
    stream << std::hex << std::uppercase << std::setfill('0');
    for (const auto byte : bytes)
    {
        stream << std::setw(2) << static_cast<unsigned>(byte);
    }
    return stream.str();
}

/// RAII wrapper around sqlite3_stmt* — same shape as the helpers in
/// NodeRegistry / PendingQueue, kept local rather than shared (the three
/// are the only users today).
class Stmt
{
  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): SQL and label are clearly distinct at call sites
    Stmt(sqlite3* database, const char* sql, const char* label)
        : database_(database),
          label_(label)
    {
        if (sqlite3_prepare_v2(database, sql, -1, &stmt_, nullptr) != SQLITE_OK)
        {
            Logger::error(std::string("[PolicyRegister] prepare ") + label + " failed: " + sqlite3_errmsg(database));
            stmt_ = nullptr;
        }
    }
    ~Stmt()
    {
        if (stmt_ != nullptr)
        {
            sqlite3_finalize(stmt_);
        }
    }
    Stmt(const Stmt&)                        = delete;
    auto operator=(const Stmt&) -> Stmt&     = delete;
    Stmt(Stmt&&) noexcept                    = delete;
    auto operator=(Stmt&&) noexcept -> Stmt& = delete;

    [[nodiscard]] auto valid() const -> bool
    {
        return stmt_ != nullptr;
    }
    auto bindText(int pos, const std::string& value) -> Stmt&
    {
        sqlite3_bind_text(stmt_, pos, value.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }
    auto bindInt(int pos, int value) -> Stmt&
    {
        sqlite3_bind_int(stmt_, pos, value);
        return *this;
    }
    auto bindBlob(int pos, const std::vector<std::uint8_t>& value) -> Stmt&
    {
        sqlite3_bind_blob(stmt_, pos, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        return *this;
    }
    auto step() -> int
    {
        return sqlite3_step(stmt_);
    }
    auto execDone() -> void
    {
        if (sqlite3_step(stmt_) != SQLITE_DONE)
        {
            Logger::error(std::string("[PolicyRegister] ") + label_ + " failed: " + sqlite3_errmsg(database_));
        }
    }
    [[nodiscard]] auto columnBlob(int col) const -> std::vector<std::uint8_t>
    {
        const auto* data = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt_, col));
        const int size   = sqlite3_column_bytes(stmt_, col);
        if (data == nullptr || size <= 0)
        {
            return {};
        }
        return {data, data + size};
    }

  private:
    sqlite3_stmt* stmt_ = nullptr;
    sqlite3* database_  = nullptr;
    const char* label_  = nullptr;
};
}  // namespace

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): pimpl, public members read like a struct
struct PolicyRegister::Register::State
{
    mutable std::mutex mutex;
    sqlite3* db = nullptr;
    std::optional<std::string> currentHomeId;
    std::map<std::pair<std::string, std::uint8_t>, DeviceId> identityCache;

    State()                                    = default;
    State(const State&)                        = delete;
    auto operator=(const State&) -> State&     = delete;
    State(State&&) noexcept                    = delete;
    auto operator=(State&&) noexcept -> State& = delete;
    ~State()
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

PolicyRegister::Register::Register(const std::filesystem::path& dbPath)
    : state_(std::make_unique<State>())
{
    std::error_code errorCode;
    std::filesystem::create_directories(dbPath.parent_path(), errorCode);
    if (errorCode)
    {
        Logger::error("[PolicyRegister] cannot create state dir " + dbPath.parent_path().string() + ": " +
                      errorCode.message());
        return;
    }
    if (sqlite3_open(dbPath.c_str(), &state_->db) != SQLITE_OK)
    {
        Logger::error("[PolicyRegister] cannot open " + dbPath.string() + ": " + sqlite3_errmsg(state_->db));
        sqlite3_close(state_->db);
        state_->db = nullptr;
        return;
    }
    char* err = nullptr;
    if (sqlite3_exec(state_->db, SCHEMA_SQL, nullptr, nullptr, &err) != SQLITE_OK)
    {
        Logger::error(std::string("[PolicyRegister] CREATE TABLE failed: ") + (err != nullptr ? err : "?"));
        sqlite3_free(err);
        sqlite3_close(state_->db);
        state_->db = nullptr;
        return;
    }
    Logger::info("[PolicyRegister] db ready at " + dbPath.string());
}

PolicyRegister::Register::~Register() = default;

auto PolicyRegister::Register::setHomeId(const std::vector<std::uint8_t>& homeIdBytes) -> void
{
    const std::scoped_lock lock(state_->mutex);
    state_->currentHomeId = formatHomeId(homeIdBytes);
}

auto PolicyRegister::Register::setDevicePolicy(DeviceId device, const Policy& policy) -> void
{
    {
        const std::scoped_lock lock(state_->mutex);
        if (state_->db == nullptr)
        {
            return;
        }
        Stmt stmt(state_->db, UPSERT_DEVICE_SQL, "UPSERT device");
        if (!stmt.valid())
        {
            return;
        }
        stmt.bindInt(1, device.manufacturerId)
            .bindInt(2, device.productTypeId)
            .bindInt(3, device.productId)
            .bindBlob(4, serialize(policy))
            .execDone();
    }
    // nodeId 0 == device-level change; orchestrators treat it as wildcard.
    MessageBus::publish(MessageBus::PolicyChanged{.nodeId = 0});
}

auto PolicyRegister::Register::setNodeOverride(std::uint8_t nodeId, const Policy& policy) -> void
{
    {
        const std::scoped_lock lock(state_->mutex);
        if (state_->db == nullptr || !state_->currentHomeId.has_value())
        {
            Logger::warn("[PolicyRegister] setNodeOverride dropped — no DB / no home (node " + std::to_string(nodeId) +
                         ")");
            return;
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
        const std::string& home = *state_->currentHomeId;
        Stmt stmt(state_->db, UPSERT_NODE_SQL, "UPSERT node");
        if (!stmt.valid())
        {
            return;
        }
        stmt.bindText(1, home).bindInt(2, nodeId).bindBlob(3, serialize(policy)).execDone();
    }
    MessageBus::publish(MessageBus::PolicyChanged{.nodeId = nodeId});
}

auto PolicyRegister::Register::deleteDevicePolicy(DeviceId device) -> void
{
    {
        const std::scoped_lock lock(state_->mutex);
        if (state_->db == nullptr)
        {
            return;
        }
        Stmt stmt(state_->db, DELETE_DEVICE_SQL, "DELETE device");
        if (!stmt.valid())
        {
            return;
        }
        stmt.bindInt(1, device.manufacturerId).bindInt(2, device.productTypeId).bindInt(3, device.productId).execDone();
    }
    MessageBus::publish(MessageBus::PolicyChanged{.nodeId = 0});
}

auto PolicyRegister::Register::deleteNodeOverride(std::uint8_t nodeId) -> void
{
    {
        const std::scoped_lock lock(state_->mutex);
        if (state_->db == nullptr || !state_->currentHomeId.has_value())
        {
            return;
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
        const std::string& home = *state_->currentHomeId;
        Stmt stmt(state_->db, DELETE_NODE_SQL, "DELETE node");
        if (!stmt.valid())
        {
            return;
        }
        stmt.bindText(1, home).bindInt(2, nodeId).execDone();
    }
    MessageBus::publish(MessageBus::PolicyChanged{.nodeId = nodeId});
}

auto PolicyRegister::Register::devicePolicy(DeviceId device) const -> std::optional<Policy>
{
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr)
    {
        return std::nullopt;
    }
    Stmt stmt(state_->db, SELECT_DEVICE_SQL, "SELECT device");
    if (!stmt.valid())
    {
        return std::nullopt;
    }
    stmt.bindInt(1, device.manufacturerId).bindInt(2, device.productTypeId).bindInt(3, device.productId);
    if (stmt.step() != SQLITE_ROW)
    {
        return std::nullopt;
    }
    return deserialize(stmt.columnBlob(0));
}

auto PolicyRegister::Register::nodeOverride(std::uint8_t nodeId) const -> std::optional<Policy>
{
    const std::scoped_lock lock(state_->mutex);
    if (state_->db == nullptr || !state_->currentHomeId.has_value())
    {
        return std::nullopt;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home = *state_->currentHomeId;
    Stmt stmt(state_->db, SELECT_NODE_SQL, "SELECT node");
    if (!stmt.valid())
    {
        return std::nullopt;
    }
    stmt.bindText(1, home).bindInt(2, nodeId);
    if (stmt.step() != SQLITE_ROW)
    {
        return std::nullopt;
    }
    return deserialize(stmt.columnBlob(0));
}

auto PolicyRegister::Register::noteDeviceIdentity(std::uint8_t nodeId, DeviceId device) -> void
{
    const std::scoped_lock lock(state_->mutex);
    if (!state_->currentHomeId.has_value())
    {
        return;
    }
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
    const std::string& home               = *state_->currentHomeId;
    state_->identityCache[{home, nodeId}] = device;
}

auto PolicyRegister::Register::effectivePolicy(std::uint8_t nodeId) const -> Policy
{
    std::optional<DeviceId> identity;
    {
        const std::scoped_lock lock(state_->mutex);
        if (state_->currentHomeId.has_value())
        {
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access): checked above; tidy can't track the short-circuit
            const std::string& home = *state_->currentHomeId;
            const auto iter         = state_->identityCache.find({home, nodeId});
            if (iter != state_->identityCache.end())
            {
                identity = iter->second;
            }
        }
    }

    // Per-node override and device default are fetched through the public
    // accessors (each takes the lock itself) so this method holds no lock
    // across them — keeps the locking flat and reentrancy-free.
    const std::optional<Policy> override = nodeOverride(nodeId);
    std::optional<Policy> deviceDefault;
    if (identity.has_value())
    {
        deviceDefault = devicePolicy(*identity);
    }

    if (deviceDefault.has_value() && override.has_value())
    {
        return merge(*deviceDefault, *override);
    }
    if (override.has_value())
    {
        return *override;
    }
    if (deviceDefault.has_value())
    {
        return *deviceDefault;
    }
    return {};
}

// ---- Production singleton --------------------------------------------

namespace
{
struct SingletonState
{
    std::string configuredStateDir;
    MessageBus::SubscriptionGuard storageSub;
    MessageBus::SubscriptionGuard manufacturerSub;
    std::unique_ptr<PolicyRegister::Register> reg;
    std::once_flag initFlag;
};

auto singletonState() -> SingletonState&
{
    static SingletonState instance;
    return instance;
}

auto resolveDbPath() -> std::filesystem::path
{
    if (!singletonState().configuredStateDir.empty())
    {
        return std::filesystem::path(singletonState().configuredStateDir) / DB_FILENAME;
    }
    // NOLINTNEXTLINE(concurrency-mt-unsafe): runs once during call_once-protected init
    const char* env       = std::getenv(STATE_DIR_ENV);
    const std::string dir = (env != nullptr && *env != '\0') ? env : DEFAULT_STATE_DIR;
    return std::filesystem::path(dir) / DB_FILENAME;
}
}  // namespace

auto PolicyRegister::instance() -> Register&
{
    std::call_once(singletonState().initFlag,
                   []
                   {
                       singletonState().storageSub =
                           MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::StorageConfig>(
                               [](const MessageBus::StorageConfig& cfg) -> void
                               { singletonState().configuredStateDir = cfg.stateDir; }));
                       singletonState().reg = std::make_unique<Register>(resolveDbPath());
                       // Keep the device-identity cache warm from inbound
                       // ManufacturerSpecific Reports so effectivePolicy can find
                       // the device default without the caller knowing the triple.
                       singletonState().manufacturerSub =
                           MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::ManufacturerSpecificReport>(
                               [](const MessageBus::ManufacturerSpecificReport& report) -> void
                               {
                                   singletonState().reg->noteDeviceIdentity(report.sourceNodeId,
                                                                            DeviceId{
                                                                                .manufacturerId = report.manufacturerId,
                                                                                .productTypeId  = report.productTypeId,
                                                                                .productId      = report.productId,
                                                                            });
                               }));
                   });
    return *singletonState().reg;
}
