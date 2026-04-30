#include "HostApi.hpp"

#include "ZwaveDataFrame.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace
{
constexpr uint8_t MODE_MASK         = 0x0F;
constexpr uint8_t FLAG_POWER_BIT    = 7;
constexpr uint8_t FLAG_NWI_BIT      = 6;  // Add Node
constexpr uint8_t FLAG_PROTOCOL_BIT = 5;  // Add Node
constexpr uint8_t FLAG_SFLND_BIT    = 4;  // Add Node
constexpr uint8_t FLAG_NWE_BIT      = 6;  // Remove Node

constexpr std::size_t HOMEID_LEN      = 4;
constexpr unsigned NODE_ID_HIGH_SHIFT = 8;

auto makeFlagByte(const uint8_t mode,
                  const bool power,
                  const uint8_t bitB,
                  const bool valB,
                  const uint8_t bitC,
                  const bool valC,
                  const uint8_t bitD,
                  const bool valD) -> uint8_t
{
    uint8_t out = mode & MODE_MASK;
    if (power)
    {
        out = static_cast<uint8_t>(out | (1U << FLAG_POWER_BIT));
    }
    if (valB)
    {
        out = static_cast<uint8_t>(out | (1U << bitB));
    }
    if (valC)
    {
        out = static_cast<uint8_t>(out | (1U << bitC));
    }
    if (valD)
    {
        out = static_cast<uint8_t>(out | (1U << bitD));
    }
    return out;
}
}  // namespace

auto HostApi::encodeAddNode(const AddNodeRequest& request) -> ZwaveDataFrame
{
    ZwaveDataFrame frame;
    frame.setHeader(ZwaveDataFrame::FrameType::REQUEST, CMD_ADD_NODE_TO_NETWORK);

    std::vector<uint8_t> payload;
    payload.reserve(2 + (2 * HOMEID_LEN));
    payload.push_back(makeFlagByte(request.mode,
                                   request.power,
                                   FLAG_NWI_BIT,
                                   request.nwi,
                                   FLAG_PROTOCOL_BIT,
                                   request.protocolLongRange,
                                   FLAG_SFLND_BIT,
                                   request.skipFlNeighbors));
    payload.push_back(request.sessionId);
    if (request.includeHomeIds)
    {
        for (uint8_t const byte : request.nwiHomeId)
        {
            payload.push_back(byte);
        }
        if (request.mode == ADD_MODE_SMART_START_INCLUDE)
        {
            for (uint8_t const byte : request.authHomeId)
            {
                payload.push_back(byte);
            }
        }
    }
    frame.setPayload(payload.data(), payload.size());
    return frame;
}

auto HostApi::encodeRemoveNode(const RemoveNodeRequest& request) -> ZwaveDataFrame
{
    ZwaveDataFrame frame;
    frame.setHeader(ZwaveDataFrame::FrameType::REQUEST, CMD_REMOVE_NODE_FROM_NETWORK);

    std::vector<uint8_t> payload;
    payload.reserve(2);
    auto flags = static_cast<uint8_t>(request.mode & MODE_MASK);
    if (request.power)
    {
        flags = static_cast<uint8_t>(flags | (1U << FLAG_POWER_BIT));
    }
    if (request.nwe)
    {
        flags = static_cast<uint8_t>(flags | (1U << FLAG_NWE_BIT));
    }
    payload.push_back(flags);
    payload.push_back(request.sessionId);
    frame.setPayload(payload.data(), payload.size());
    return frame;
}

auto HostApi::decodeNodeCallback(const ZwaveDataFrame& frame, bool nodeId16Bit) -> std::optional<NodeStatusCallback>
{
    if (!frame.isValid())
    {
        return std::nullopt;
    }
    uint8_t const cmd = frame.getCommand();
    if (cmd != CMD_ADD_NODE_TO_NETWORK && cmd != CMD_REMOVE_NODE_FROM_NETWORK)
    {
        return std::nullopt;
    }

    const uint8_t* payload  = frame.getPayload();
    std::size_t const total = frame.getPayloadSize();
    if (payload == nullptr || total < 3)
    {
        return std::nullopt;
    }

    NodeStatusCallback out;
    out.commandId = cmd;
    out.sessionId = payload[0];
    out.status    = payload[1];

    std::size_t offset = 2;
    if (nodeId16Bit)
    {
        if (total < offset + 2)
        {
            return out;
        }
        out.nodeId = static_cast<uint16_t>((payload[offset] << NODE_ID_HIGH_SHIFT) | payload[offset + 1]);
        offset += 2;
    }
    else
    {
        out.nodeId = payload[offset];
        offset += 1;
    }

    if (total <= offset)
    {
        return out;
    }
    std::size_t const ccLen = payload[offset];
    offset += 1;

    if (total >= offset + 3)
    {
        out.basicDeviceType    = payload[offset];
        out.genericDeviceType  = payload[offset + 1];
        out.specificDeviceType = payload[offset + 2];
        offset += 3;
    }

    if (ccLen > 0 && total >= offset + ccLen)
    {
        out.commandClasses.assign(payload + offset, payload + offset + ccLen);
    }

    return out;
}
