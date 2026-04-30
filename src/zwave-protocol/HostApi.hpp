#ifndef ZWAVED_HOST_API_HPP
#define ZWAVED_HOST_API_HPP

#include "ZwaveDataFrame.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace HostApi
{
using SessionId = uint8_t;

constexpr uint8_t CMD_APPLICATION_COMMAND      = 0x04;
constexpr uint8_t CMD_SEND_DATA                = 0x13;
constexpr uint8_t CMD_ADD_NODE_TO_NETWORK      = 0x4A;
constexpr uint8_t CMD_REMOVE_NODE_FROM_NETWORK = 0x4B;

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
[[nodiscard]] auto encodeSendData(const SendDataRequest& request) -> ZwaveDataFrame;

/// Decode a FUNC_ID_ZW_SEND_DATA (0x13) callback. Returns std::nullopt
/// if the frame is not a 0x13 callback or the payload is too short.
[[nodiscard]] auto decodeSendDataCallback(const ZwaveDataFrame& frame) -> std::optional<SendDataCallback>;

/// Decode a FUNC_ID_APPLICATION_COMMAND_HANDLER (0x04) frame. Returns
/// std::nullopt if the frame is not a 0x04 callback or the payload is
/// too short / inconsistent.
[[nodiscard]] auto decodeApplicationCommand(const ZwaveDataFrame& frame) -> std::optional<ApplicationCommand>;

/// Decode either a 0x4A or 0x4B callback. Pass nodeId16Bit = true if the
/// controller has been configured for 16-bit node IDs (default false matches
/// the Aeotec Z-Stick Gen5 baseline). Returns std::nullopt if the frame is
/// not a recognized callback or the payload is too short.
[[nodiscard]] auto decodeNodeCallback(const ZwaveDataFrame& frame,
                                      bool nodeId16Bit = false) -> std::optional<NodeStatusCallback>;
}  // namespace HostApi

#endif  // ZWAVED_HOST_API_HPP
