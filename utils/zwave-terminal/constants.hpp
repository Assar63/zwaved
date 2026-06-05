#ifndef ZWAVE_TERMINAL_CONSTANTS_HPP
#define ZWAVE_TERMINAL_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>

// Shared compile-time constants for the zwave-terminal client. See #111.
namespace zwt
{
constexpr const char* BUS_NAME    = "com.tiunda.ZWaved";
constexpr const char* OBJECT_PATH = "/com/tiunda/ZWaved";
constexpr const char* IFACE_NAME  = "com.tiunda.ZWaved1";

constexpr int UI_REFRESH_MS                  = 100;
constexpr std::size_t MAX_LOG_LINES          = 200;
constexpr std::size_t TIMESTAMP_BUFFER_BYTES = 16;

constexpr std::uint8_t MODE_CLASSIC = 0x01;
constexpr std::uint8_t FLAGS_NONE   = 0x00;

// Inclusion / exclusion status codes — see MANUAL.md §9 and the Z-Wave spec
// (tables 4.124, 4.134).
constexpr std::uint8_t STATUS_STARTED        = 0x01;
constexpr std::uint8_t STATUS_NODE_FOUND     = 0x02;
constexpr std::uint8_t STATUS_ONGOING_END    = 0x03;
constexpr std::uint8_t STATUS_ONGOING_CTRL   = 0x04;
constexpr std::uint8_t STATUS_PROTOCOL_DONE  = 0x05;
constexpr std::uint8_t STATUS_COMPLETED      = 0x06;
constexpr std::uint8_t STATUS_FAILED         = 0x07;
constexpr std::uint8_t STATUS_NEIGHBORS_DONE = 0x0B;
constexpr std::uint8_t STATUS_NOT_PRIMARY    = 0x23;

// FUNC_ID_ZW_SEND_DATA callback transmit-status values (mirrors HostApi).
constexpr std::uint8_t TX_STATUS_OK       = 0x00;
constexpr std::uint8_t TX_STATUS_NO_ACK   = 0x01;
constexpr std::uint8_t TX_STATUS_FAIL     = 0x02;
constexpr std::uint8_t TX_STATUS_NOT_IDLE = 0x03;
constexpr std::uint8_t TX_STATUS_NO_ROUTE = 0x04;
constexpr std::uint8_t TX_STATUS_VERIFIED = 0x05;

// SwitchBinaryReport state encoding (matches BinarySwitch::State).
constexpr std::uint8_t SWITCH_STATE_OFF     = 0;
constexpr std::uint8_t SWITCH_STATE_ON      = 1;
constexpr std::uint8_t SWITCH_STATE_UNKNOWN = 2;

// Command-class wire constants for decoding unsolicited binary on/off
// traffic in ApplicationCommand frames.
constexpr std::uint8_t CC_BASIC           = 0x20;
constexpr std::uint8_t CC_SWITCH_BINARY   = 0x25;
constexpr std::uint8_t CMD_SET            = 0x01;
constexpr std::uint8_t CMD_REPORT         = 0x03;
constexpr std::uint8_t WIRE_VALUE_OFF     = 0x00;
constexpr std::uint8_t WIRE_VALUE_UNKNOWN = 0xFE;

// COMMAND_CLASS_MARK separates the CCs the node *supports* (i.e.
// will respond to) from the ones it *controls* (i.e. emits to its
// associated nodes — typically Basic SET on a wall switch toggle).
constexpr std::uint8_t CC_MARK        = 0xEF;
constexpr std::uint8_t CC_ASSOCIATION = 0x85;

// callbackId=0 in SendData means "no completion callback wanted from
// the dongle"; the node's application reply still arrives normally,
// so it's perfect for fire-and-forget auto-introspection queries.
constexpr std::uint8_t CALLBACK_ID_NONE = 0;

// Valid Z-Wave 8-bit node IDs (excluding broadcast 0 and reserved >232).
constexpr int NODE_ID_MIN = 1;
constexpr int NODE_ID_MAX = 232;

// Full byte range for the Multilevel Switch level / duration prompts.
// The spec semantics ride on the sentinel values (0xFE = unknown,
// 0xFF = restore-last / default-duration), so we accept any byte and
// let the device interpret it.
constexpr int BYTE_MIN = 0x00;
constexpr int BYTE_MAX = 0xFF;

// Association group IDs are 1..255 per spec (0 reserved).
constexpr int GROUP_ID_MIN = 1;
constexpr int GROUP_ID_MAX = 255;

// Conventional Z-Wave lifeline association group.
constexpr std::uint8_t LIFELINE_GROUP = 1;

// Transition "factory default duration" sentinel (Multilevel/Color Switch).
constexpr std::uint8_t DURATION_DEFAULT = 0xFF;

// FUNC_ID values used to decode `GetNetworkStatus`'s sessionCommandId
// field — kept identical to HostApi::CMD_*; we don't include HostApi
// here, the terminal is purely a D-Bus client.
constexpr std::uint8_t CMD_ADD_NODE    = 0x4A;
constexpr std::uint8_t CMD_REMOVE_NODE = 0x4B;

// Uptime formatting.
constexpr std::uint64_t SECONDS_PER_HOUR   = 3600;
constexpr std::uint64_t SECONDS_PER_MINUTE = 60;

// Max characters of node-id input read from the bottom-row prompt
// (3 digits + null terminator, with slack).
constexpr std::size_t NODE_ID_INPUT_BUFFER = 8;

// Wider input buffer for signed Configuration values (e.g. "-2147483648").
constexpr std::size_t INT_INPUT_BUFFER = 16;
// Free-form line input (e.g. a space/comma-separated association member list).
constexpr std::size_t LINE_INPUT_BUFFER = 64;

constexpr int DECIMAL_BASE      = 10;
constexpr int HEX_BASE          = 16;
constexpr std::uint32_t U16_MAX = 0xFFFFU;
constexpr std::uint32_t U32_MAX = 0xFFFFFFFFU;

// PolicyRegister BLOB wire format (see src/policy-register/PolicyRegister.cpp).
// Reimplemented here because the terminal is a standalone D-Bus client —
// it doesn't link daemon code, same as the NodeTuple / status-code
// duplication elsewhere in this file. The leading version byte makes a
// format change detectable rather than silently misparsed.
constexpr std::uint8_t POLICY_BLOB_VERSION    = 1;
constexpr std::uint8_t POLICY_KIND_CONFIG     = 1;
constexpr std::uint8_t POLICY_KIND_ASSOC      = 2;
constexpr std::uint8_t POLICY_KIND_WAKEUP     = 3;
constexpr std::size_t POLICY_CONFIG_BODY_LEN  = 7;  // parameter, size, signed, value(4)
constexpr std::size_t POLICY_ASSOC_HEADER_LEN = 2;  // groupId, memberCount
constexpr std::size_t POLICY_WAKEUP_BODY_LEN  = 5;  // intervalSeconds(4), notificationNodeId

// Valid Configuration value sizes per the CC spec.
constexpr int CONFIG_SIZE_MIN = 1;
constexpr int CONFIG_SIZE_MAX = 4;

constexpr unsigned BITS_PER_BYTE      = 8;
constexpr std::uint32_t U32_BYTE_MASK = 0xFFU;

// DaemonError severity values (mirror MessageBus::DaemonError::SEVERITY_*).
constexpr std::uint8_t SEVERITY_INFO     = 1;
constexpr std::uint8_t SEVERITY_WARN     = 2;
constexpr std::uint8_t SEVERITY_ERROR    = 3;
constexpr std::uint8_t SEVERITY_CRITICAL = 4;

// ncurses colour-pair ids for the DaemonError banner.
constexpr int CP_WARN     = 1;
constexpr int CP_ERROR    = 2;
constexpr int CP_CRITICAL = 3;
}  // namespace zwt

#endif  // ZWAVE_TERMINAL_CONSTANTS_HPP
