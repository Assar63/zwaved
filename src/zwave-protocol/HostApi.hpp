#ifndef ZWAVED_HOST_API_HPP
#define ZWAVED_HOST_API_HPP

#include "ZwaveDataFrame.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace HostApi
{
using SessionId = uint8_t;

constexpr uint8_t CMD_SERIAL_API_GET_INIT_DATA = 0x02;
constexpr uint8_t CMD_APPLICATION_COMMAND      = 0x04;
constexpr uint8_t CMD_SEND_DATA                = 0x13;
constexpr uint8_t CMD_GET_VERSION              = 0x15;
constexpr uint8_t CMD_MEMORY_GET_ID            = 0x20;
constexpr uint8_t CMD_GET_NODE_PROTOCOL_INFO   = 0x41;
constexpr uint8_t CMD_ADD_NODE_TO_NETWORK      = 0x4A;
constexpr uint8_t CMD_REMOVE_NODE_FROM_NETWORK = 0x4B;
constexpr uint8_t CMD_APPLICATION_UPDATE       = 0x49;
constexpr uint8_t CMD_REQUEST_NODE_INFO        = 0x60;
constexpr uint8_t CMD_REMOVE_FAILED_NODE_ID    = 0x61;

// FUNC_ID_GET_VERSION library types (response byte 12).
constexpr uint8_t LIBRARY_TYPE_STATIC_CONTROLLER = 1;
constexpr uint8_t LIBRARY_TYPE_CONTROLLER        = 2;
constexpr uint8_t LIBRARY_TYPE_ENHANCED_SLAVE    = 3;
constexpr uint8_t LIBRARY_TYPE_SLAVE             = 4;
constexpr uint8_t LIBRARY_TYPE_INSTALLER         = 5;
constexpr uint8_t LIBRARY_TYPE_ROUTING_SLAVE     = 6;
constexpr uint8_t LIBRARY_TYPE_BRIDGE_CONTROLLER = 7;
constexpr uint8_t LIBRARY_TYPE_DUT               = 8;

// FUNC_ID_ZW_SEND_DATA transmit options.
constexpr uint8_t TRANSMIT_OPTION_ACK        = 0x01;
constexpr uint8_t TRANSMIT_OPTION_AUTO_ROUTE = 0x04;
constexpr uint8_t TRANSMIT_OPTION_EXPLORE    = 0x20;
constexpr uint8_t TRANSMIT_OPTION_DEFAULT = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE | TRANSMIT_OPTION_EXPLORE;

// FUNC_ID_ZW_SEND_DATA callback transmit-status values.
constexpr uint8_t TRANSMIT_COMPLETE_OK       = 0x00;
constexpr uint8_t TRANSMIT_COMPLETE_NO_ACK   = 0x01;
constexpr uint8_t TRANSMIT_COMPLETE_FAIL     = 0x02;
constexpr uint8_t TRANSMIT_NOT_IDLE          = 0x03;
constexpr uint8_t TRANSMIT_COMPLETE_NO_ROUTE = 0x04;
constexpr uint8_t TRANSMIT_COMPLETE_VERIFIED = 0x05;

constexpr uint8_t ADD_MODE_ANY_NODE            = 0x01;
constexpr uint8_t ADD_MODE_STOP                = 0x05;
constexpr uint8_t ADD_MODE_STOP_REPLICATION    = 0x06;
constexpr uint8_t ADD_MODE_SMART_START_INCLUDE = 0x08;
constexpr uint8_t ADD_MODE_SMART_START_LISTEN  = 0x09;

constexpr uint8_t REMOVE_MODE_ANY_NODE = 0x01;
constexpr uint8_t REMOVE_MODE_STOP     = 0x05;

/// Initial-frame parameters for FUNC_ID_ZW_ADD_NODE_TO_NETWORK (0x4A).
struct AddNodeRequest
{
    uint8_t mode           = ADD_MODE_ANY_NODE;
    bool power             = false;
    bool nwi               = false;
    bool protocolLongRange = false;
    bool skipFlNeighbors   = false;
    SessionId sessionId    = 0;
    bool includeHomeIds    = false;
    std::array<uint8_t, 4> nwiHomeId{};
    std::array<uint8_t, 4> authHomeId{};
};

/// Initial-frame parameters for FUNC_ID_ZW_REMOVE_NODE_FROM_NETWORK (0x4B).
struct RemoveNodeRequest
{
    uint8_t mode        = REMOVE_MODE_ANY_NODE;
    bool power          = false;
    bool nwe            = false;
    SessionId sessionId = 0;
};

/// Application-layer command request: deliver `data` to `nodeId` over
/// FUNC_ID_ZW_SEND_DATA (0x13). `data` is the raw command-class payload
/// (e.g. {COMMAND_CLASS_SWITCH_BINARY, SWITCH_BINARY_SET, value}).
struct SendDataRequest
{
    uint8_t nodeId = 0;
    std::vector<uint8_t> data;
    uint8_t txOptions  = TRANSMIT_OPTION_DEFAULT;
    uint8_t callbackId = 0;
};

/// Decoded callback payload for FUNC_ID_ZW_SEND_DATA (0x13). The dongle
/// emits this once transmission to the destination node completes (or
/// fails). Newer firmwares append transmit-time and route bytes — we
/// only consume the prefix needed to report success/failure.
struct SendDataCallback
{
    uint8_t callbackId = 0;
    uint8_t txStatus   = TRANSMIT_COMPLETE_FAIL;
};

/// Decoded payload of FUNC_ID_GET_VERSION (0x15) RESPONSE. The version
/// string is the dongle's printable Z-Wave library version (e.g.
/// "Z-Wave 6.07"); libraryType is one of the LIBRARY_TYPE_* constants.
struct VersionResponse
{
    std::string version;
    uint8_t libraryType = 0;
};

/// Decoded payload of FUNC_ID_MEMORY_GET_ID (0x20) RESPONSE. Home ID
/// is the 4-byte network identifier (big-endian as transmitted);
/// controllerNodeId is this controller's own node ID inside the network.
struct MemoryIdResponse
{
    std::array<uint8_t, 4> homeId{};
    uint8_t controllerNodeId = 0;
};

/// Decoded payload of FUNC_ID_SERIAL_API_GET_INIT_DATA (0x02) RESPONSE.
/// Returned during the dongle's startup introspection — nodeIds is the
/// expanded node bitmap (every nodeId currently included in the
/// network, regardless of whether the daemon has met it during this
/// run). Capabilities bits per INS12350: 0=secondary, 1=no-send,
/// 2=SIS, 3=real-primary.
struct InitDataResponse
{
    uint8_t serialApiVersion = 0;
    uint8_t capabilities     = 0;
    uint8_t chipType         = 0;
    uint8_t chipVersion      = 0;
    std::vector<uint8_t> nodeIds;
};

/// Decoded payload of FUNC_ID_GET_NODE_PROTOCOL_INFO (0x41) RESPONSE.
/// Pure controller-local query — the dongle answers from its own NVM
/// without going on-air, so it works for sleeping or out-of-range
/// nodes too. Spec: INS12350 §4.3.2.
struct NodeProtocolInfoResponse
{
    uint8_t capabilities       = 0;  // listening, baud, protocol-version bits
    uint8_t security           = 0;  // beam, routing-slave, sensor flags
    uint8_t reserved           = 0;
    uint8_t basicDeviceType    = 0;
    uint8_t genericDeviceType  = 0;
    uint8_t specificDeviceType = 0;
};

/// Decoded payload of FUNC_ID_APPLICATION_COMMAND_HANDLER (0x04). Emitted
/// by the controller whenever a node sends an unsolicited Command Class
/// frame (e.g. SwitchBinary REPORT after a manual toggle, sensor pings,
/// etc.). The 8-bit sourceNodeId form is decoded; 16-bit Long Range node
/// IDs are not supported yet.
struct ApplicationCommand
{
    uint8_t rxStatus     = 0;
    uint8_t sourceNodeId = 0;
    std::vector<uint8_t> ccData;
};

/// Initial-frame parameters for FUNC_ID_ZW_REMOVE_FAILED_NODE_ID (0x61).
/// Removes an unresponsive node from the controller's failed-node list.
/// The dongle answers synchronously with a Response frame (whether the
/// removal could start) and, if it could, later with a Callback frame
/// carrying the operation outcome.
struct RemoveFailedNodeRequest
{
    uint8_t nodeId      = 0;
    SessionId sessionId = 0;
};

/// Decoded RESPONSE frame for FUNC_ID_ZW_REMOVE_FAILED_NODE_ID (0x61) —
/// indicates whether the dongle accepted the removal request. Final
/// outcome arrives separately as a callback (RemoveFailedNodeCallback).
struct RemoveFailedNodeResponse
{
    static constexpr uint8_t STATUS_STARTED        = 0x00;
    static constexpr uint8_t STATUS_NOT_PRIMARY    = 0x02;
    static constexpr uint8_t STATUS_NO_CALLBACK    = 0x04;
    static constexpr uint8_t STATUS_NODE_NOT_FOUND = 0x08;
    static constexpr uint8_t STATUS_PROCESS_BUSY   = 0x10;
    static constexpr uint8_t STATUS_REMOVE_FAIL    = 0x20;

    uint8_t status = STATUS_REMOVE_FAIL;
};

/// Decoded CALLBACK frame for FUNC_ID_ZW_REMOVE_FAILED_NODE_ID (0x61) —
/// emitted only if the response carried STATUS_STARTED. Reports the
/// final outcome of the removal.
struct RemoveFailedNodeCallback
{
    static constexpr uint8_t STATUS_NODE_OK     = 0x00;
    static constexpr uint8_t STATUS_REMOVED     = 0x01;
    static constexpr uint8_t STATUS_NOT_REMOVED = 0x02;

    SessionId sessionId = 0;
    uint8_t status      = STATUS_NOT_REMOVED;
};

/// Initial-frame parameters for FUNC_ID_ZW_REQUEST_NODE_INFO (0x60).
/// Asks the dongle to send a NodeInformation Frame request to the
/// target node. The dongle answers synchronously with a 1-byte
/// Response (accepted / not); the actual NodeInfo arrives later as
/// a FUNC_ID_APPLICATION_UPDATE (0x49) frame correlated by nodeId
/// (not by sessionId — the wire FUNC_ID carries neither).
struct RequestNodeInfoRequest
{
    uint8_t nodeId      = 0;
    SessionId sessionId = 0;
};

/// Decoded RESPONSE frame for FUNC_ID_ZW_REQUEST_NODE_INFO (0x60) —
/// indicates whether the dongle accepted the request and will attempt
/// to ferry it over the air. The actual node-info payload arrives
/// later as a FUNC_ID_APPLICATION_UPDATE frame (see ApplicationUpdate).
struct RequestNodeInfoResponse
{
    bool accepted = false;
};

/// Decoded payload of FUNC_ID_APPLICATION_UPDATE (0x49). Fires
/// asynchronously from the dongle — both in response to a prior
/// REQUEST_NODE_INFO and unsolicited (a node sending a NIF after
/// waking up). `status` distinguishes the case; payload fields are
/// only populated for STATUS_NODE_INFO_RECEIVED.
struct ApplicationUpdate
{
    static constexpr uint8_t STATUS_SUC_ID                = 0x80;
    static constexpr uint8_t STATUS_NODE_INFO_REQ_FAILED  = 0x81;
    static constexpr uint8_t STATUS_NODE_INFO_REQ_DONE    = 0x82;
    static constexpr uint8_t STATUS_NODE_INFO_RX_RECEIVED = 0x83;
    static constexpr uint8_t STATUS_NODE_INFO_RECEIVED    = 0x84;
    static constexpr uint8_t STATUS_NEW_ID_ASSIGNED       = 0x86;

    uint8_t status             = 0;
    uint8_t nodeId             = 0;
    uint8_t basicDeviceType    = 0;
    uint8_t genericDeviceType  = 0;
    uint8_t specificDeviceType = 0;
    std::vector<uint8_t> commandClasses;
};

/// Decoded callback payload for either Add (0x4A) or Remove (0x4B).
struct NodeStatusCallback
{
    uint8_t commandId          = 0;
    SessionId sessionId        = 0;
    uint8_t status             = 0;
    uint16_t nodeId            = 0;
    uint8_t basicDeviceType    = 0;
    uint8_t genericDeviceType  = 0;
    uint8_t specificDeviceType = 0;
    std::vector<uint8_t> commandClasses;
};

[[nodiscard]] auto encodeAddNode(const AddNodeRequest& request) -> ZwaveDataFrame;
[[nodiscard]] auto encodeRemoveNode(const RemoveNodeRequest& request) -> ZwaveDataFrame;
[[nodiscard]] auto encodeRemoveFailedNode(const RemoveFailedNodeRequest& request) -> ZwaveDataFrame;
[[nodiscard]] auto encodeRequestNodeInfo(const RequestNodeInfoRequest& request) -> ZwaveDataFrame;
[[nodiscard]] auto encodeSendData(const SendDataRequest& request) -> ZwaveDataFrame;
[[nodiscard]] auto encodeGetNodeProtocolInfo(uint8_t nodeId) -> ZwaveDataFrame;

/// Decode a FUNC_ID_ZW_SEND_DATA (0x13) callback. Returns std::nullopt
/// if the frame is not a 0x13 callback or the payload is too short.
[[nodiscard]] auto decodeSendDataCallback(const ZwaveDataFrame& frame) -> std::optional<SendDataCallback>;

/// Decode a FUNC_ID_APPLICATION_COMMAND_HANDLER (0x04) frame. Returns
/// std::nullopt if the frame is not a 0x04 callback or the payload is
/// too short / inconsistent.
[[nodiscard]] auto decodeApplicationCommand(const ZwaveDataFrame& frame) -> std::optional<ApplicationCommand>;

/// Decode a FUNC_ID_GET_VERSION (0x15) RESPONSE.
[[nodiscard]] auto decodeVersion(const ZwaveDataFrame& frame) -> std::optional<VersionResponse>;

/// Decode a FUNC_ID_MEMORY_GET_ID (0x20) RESPONSE.
[[nodiscard]] auto decodeMemoryId(const ZwaveDataFrame& frame) -> std::optional<MemoryIdResponse>;

/// Decode a FUNC_ID_SERIAL_API_GET_INIT_DATA (0x02) RESPONSE; expands
/// the node bitmap into a sorted nodeIds vector (only valid 1..232
/// node IDs are emitted).
[[nodiscard]] auto decodeInitData(const ZwaveDataFrame& frame) -> std::optional<InitDataResponse>;

/// Decode a FUNC_ID_GET_NODE_PROTOCOL_INFO (0x41) RESPONSE. Returns
/// std::nullopt for non-matching frames or short payloads. A six-byte
/// payload of all zeros indicates the controller's NVM has no entry
/// for the requested node — treat as "node not known," not as a real
/// node with all-zero device class.
[[nodiscard]] auto decodeNodeProtocolInfo(const ZwaveDataFrame& frame) -> std::optional<NodeProtocolInfoResponse>;

/// Decode either a 0x4A or 0x4B callback. Pass nodeId16Bit = true if the
/// controller has been configured for 16-bit node IDs (default false matches
/// the Aeotec Z-Stick Gen5 baseline). Returns std::nullopt if the frame is
/// not a recognized callback or the payload is too short.
[[nodiscard]] auto decodeNodeCallback(const ZwaveDataFrame& frame,
                                      bool nodeId16Bit = false) -> std::optional<NodeStatusCallback>;

/// Decode the RESPONSE frame for FUNC_ID_ZW_REMOVE_FAILED_NODE_ID (0x61).
[[nodiscard]] auto decodeRemoveFailedNodeResponse(const ZwaveDataFrame& frame)
    -> std::optional<RemoveFailedNodeResponse>;

/// Decode the CALLBACK frame for FUNC_ID_ZW_REMOVE_FAILED_NODE_ID (0x61).
[[nodiscard]] auto decodeRemoveFailedNodeCallback(const ZwaveDataFrame& frame)
    -> std::optional<RemoveFailedNodeCallback>;

/// Decode the RESPONSE frame for FUNC_ID_ZW_REQUEST_NODE_INFO (0x60).
/// Returns std::nullopt for non-matching frames.
[[nodiscard]] auto decodeRequestNodeInfoResponse(const ZwaveDataFrame& frame) -> std::optional<RequestNodeInfoResponse>;

/// Decode an asynchronous FUNC_ID_APPLICATION_UPDATE (0x49) frame. The
/// dongle emits these both in response to a prior REQUEST_NODE_INFO
/// and spontaneously when a node sends a NIF (e.g. on wake-up).
/// device-class + commandClasses are only populated when status is
/// STATUS_NODE_INFO_RECEIVED. Returns std::nullopt for non-matching
/// frames or truncated payloads.
[[nodiscard]] auto decodeApplicationUpdate(const ZwaveDataFrame& frame) -> std::optional<ApplicationUpdate>;
}  // namespace HostApi

#endif  // ZWAVED_HOST_API_HPP
