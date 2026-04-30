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

constexpr uint8_t CMD_ADD_NODE_TO_NETWORK      = 0x4A;
constexpr uint8_t CMD_REMOVE_NODE_FROM_NETWORK = 0x4B;

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

/// Decode either a 0x4A or 0x4B callback. Pass nodeId16Bit = true if the
/// controller has been configured for 16-bit node IDs (default false matches
/// the Aeotec Z-Stick Gen5 baseline). Returns std::nullopt if the frame is
/// not a recognized callback or the payload is too short.
[[nodiscard]] auto decodeNodeCallback(const ZwaveDataFrame& frame,
                                      bool nodeId16Bit = false) -> std::optional<NodeStatusCallback>;
}  // namespace HostApi

#endif  // ZWAVED_HOST_API_HPP
