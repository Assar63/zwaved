#include "HostApi.hpp"
#include "ZwaveDataFrame.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

namespace
{
/// Build a ZwaveDataFrame from a raw on-the-wire byte sequence (SOF +
/// length + type + cmd + payload + checksum). Used to feed the
/// HostApi decoders the exact bytes a real dongle would send.
auto frameFromBytes(const std::vector<std::uint8_t>& bytes) -> ZwaveDataFrame
{
    ZwaveDataFrame frame;
    EXPECT_TRUE(frame.parseFromBuffer(bytes.data(), bytes.size()));
    return frame;
}

/// XOR-based checksum used by the Z-Wave Serial API frame format
/// (INS12350 §6.4): seed 0xFF, XOR every byte from LEN through the
/// last data byte (i.e. excluding SOF and the checksum byte itself).
auto computeChecksum(std::span<const std::uint8_t> withoutChecksum) -> std::uint8_t
{
    std::uint8_t sum = 0xFF;
    // Skip the SOF (first byte) — the checksum covers LEN..lastData.
    for (std::size_t i = 1; i < withoutChecksum.size(); ++i)
    {
        sum ^= withoutChecksum[i];
    }
    return sum;
}

auto buildResponseFrame(std::uint8_t commandId, const std::vector<std::uint8_t>& payload) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> bytes;
    bytes.push_back(ZwaveDataFrame::SOF);
    // length = type + cmd + payload + checksum
    bytes.push_back(static_cast<std::uint8_t>(2 + payload.size() + 1));
    bytes.push_back(static_cast<std::uint8_t>(ZwaveDataFrame::FrameType::RESPONSE));
    bytes.push_back(commandId);
    for (const auto byte : payload)
    {
        bytes.push_back(byte);
    }
    bytes.push_back(computeChecksum(std::span<const std::uint8_t>(bytes.data(), bytes.size())));
    return bytes;
}

auto buildRequestFrame(std::uint8_t commandId, const std::vector<std::uint8_t>& payload) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> bytes;
    bytes.push_back(ZwaveDataFrame::SOF);
    bytes.push_back(static_cast<std::uint8_t>(2 + payload.size() + 1));
    bytes.push_back(static_cast<std::uint8_t>(ZwaveDataFrame::FrameType::REQUEST));
    bytes.push_back(commandId);
    for (const auto byte : payload)
    {
        bytes.push_back(byte);
    }
    bytes.push_back(computeChecksum(std::span<const std::uint8_t>(bytes.data(), bytes.size())));
    return bytes;
}
}  // namespace

// ===========================================================================
// encodeAddNode (FUNC_ID_ZW_ADD_NODE_TO_NETWORK = 0x4A)
// ===========================================================================

TEST(HostApi, EncodeAddNodeClassic)
{
    HostApi::AddNodeRequest req{};
    req.mode         = HostApi::ADD_MODE_ANY_NODE;  // 0x01
    req.sessionId    = 0x42;
    const auto frame = HostApi::encodeAddNode(req);
    EXPECT_TRUE(frame.isValid());
    EXPECT_EQ(frame.getCommand(), HostApi::CMD_ADD_NODE_TO_NETWORK);
    EXPECT_EQ(frame.getType(), ZwaveDataFrame::FrameType::REQUEST);
    ASSERT_GE(frame.getPayloadSize(), 2);
    // First payload byte is the flag-encoded mode; for plain classic
    // (no power, no NWI, no protocol, no SFLND) the byte is just the mode.
    EXPECT_EQ(frame.getPayload()[0], 0x01);
    EXPECT_EQ(frame.getPayload()[1], 0x42);  // sessionId
}

TEST(HostApi, EncodeAddNodeWithFlags)
{
    HostApi::AddNodeRequest req{};
    req.mode         = HostApi::ADD_MODE_ANY_NODE;
    req.power        = true;  // bit 7 = 0x80
    req.nwi          = true;  // bit 6 = 0x40
    req.sessionId    = 0x10;
    const auto frame = HostApi::encodeAddNode(req);
    ASSERT_GE(frame.getPayloadSize(), 2);
    EXPECT_EQ(frame.getPayload()[0], 0x01 | 0x80 | 0x40);
    EXPECT_EQ(frame.getPayload()[1], 0x10);
}

TEST(HostApi, EncodeAddNodeStop)
{
    HostApi::AddNodeRequest req{};
    req.mode         = HostApi::ADD_MODE_STOP;  // 0x05
    req.sessionId    = 0x42;
    const auto frame = HostApi::encodeAddNode(req);
    ASSERT_GE(frame.getPayloadSize(), 2);
    EXPECT_EQ(frame.getPayload()[0], 0x05);
    EXPECT_EQ(frame.getPayload()[1], 0x42);
}

TEST(HostApi, EncodeAddNodeSmartStartIncludeCarriesBothHomeIds)
{
    HostApi::AddNodeRequest req{};
    req.mode           = HostApi::ADD_MODE_SMART_START_INCLUDE;  // 0x08
    req.sessionId      = 0x55;
    req.includeHomeIds = true;
    req.nwiHomeId      = {0xAA, 0xBB, 0xCC, 0xDD};
    req.authHomeId     = {0x11, 0x22, 0x33, 0x44};
    const auto frame   = HostApi::encodeAddNode(req);
    ASSERT_GE(frame.getPayloadSize(), 2 + 4 + 4);
    const auto* payload = frame.getPayload();
    EXPECT_EQ(payload[0], 0x08);
    EXPECT_EQ(payload[1], 0x55);
    EXPECT_EQ(payload[2], 0xAA);
    EXPECT_EQ(payload[3], 0xBB);
    EXPECT_EQ(payload[4], 0xCC);
    EXPECT_EQ(payload[5], 0xDD);
    EXPECT_EQ(payload[6], 0x11);
    EXPECT_EQ(payload[7], 0x22);
    EXPECT_EQ(payload[8], 0x33);
    EXPECT_EQ(payload[9], 0x44);
}

// ===========================================================================
// encodeRemoveNode (FUNC_ID_ZW_REMOVE_NODE_FROM_NETWORK = 0x4B)
// ===========================================================================

TEST(HostApi, EncodeRemoveNodeClassic)
{
    HostApi::RemoveNodeRequest req{};
    req.mode         = HostApi::REMOVE_MODE_ANY_NODE;
    req.sessionId    = 0x99;
    const auto frame = HostApi::encodeRemoveNode(req);
    EXPECT_EQ(frame.getCommand(), HostApi::CMD_REMOVE_NODE_FROM_NETWORK);
    ASSERT_GE(frame.getPayloadSize(), 2);
    EXPECT_EQ(frame.getPayload()[0], 0x01);
    EXPECT_EQ(frame.getPayload()[1], 0x99);
}

TEST(HostApi, EncodeRemoveNodeStop)
{
    HostApi::RemoveNodeRequest req{};
    req.mode         = HostApi::REMOVE_MODE_STOP;  // 0x05
    req.sessionId    = 0x77;
    const auto frame = HostApi::encodeRemoveNode(req);
    ASSERT_GE(frame.getPayloadSize(), 2);
    EXPECT_EQ(frame.getPayload()[0], 0x05);
    EXPECT_EQ(frame.getPayload()[1], 0x77);
}

// ===========================================================================
// encodeRemoveFailedNode (FUNC_ID_ZW_REMOVE_FAILED_NODE_ID = 0x61)
// ===========================================================================

TEST(HostApi, EncodeRemoveFailedNode)
{
    HostApi::RemoveFailedNodeRequest req{};
    req.nodeId       = 0x0B;
    req.sessionId    = 0x07;
    const auto frame = HostApi::encodeRemoveFailedNode(req);
    EXPECT_EQ(frame.getCommand(), HostApi::CMD_REMOVE_FAILED_NODE_ID);
    ASSERT_GE(frame.getPayloadSize(), 2);
    EXPECT_EQ(frame.getPayload()[0], 0x0B);
    EXPECT_EQ(frame.getPayload()[1], 0x07);
}

// ===========================================================================
// encodeSendData (FUNC_ID_ZW_SEND_DATA = 0x13)
// ===========================================================================

TEST(HostApi, EncodeSendDataWrapsPayload)
{
    HostApi::SendDataRequest req{};
    req.nodeId       = 0x05;
    req.data         = {0x25, 0x01, 0xFF};  // BinarySwitch SET ON
    req.txOptions    = HostApi::TRANSMIT_OPTION_DEFAULT;
    req.callbackId   = 0x42;
    const auto frame = HostApi::encodeSendData(req);
    EXPECT_EQ(frame.getCommand(), HostApi::CMD_SEND_DATA);
    ASSERT_EQ(frame.getPayloadSize(), 1 + 1 + 3 + 1 + 1);
    const auto* payload = frame.getPayload();
    EXPECT_EQ(payload[0], 0x05);  // nodeId
    EXPECT_EQ(payload[1], 0x03);  // CC payload length
    EXPECT_EQ(payload[2], 0x25);  // CC byte 0
    EXPECT_EQ(payload[3], 0x01);
    EXPECT_EQ(payload[4], 0xFF);
    EXPECT_EQ(payload[5], HostApi::TRANSMIT_OPTION_DEFAULT);
    EXPECT_EQ(payload[6], 0x42);  // callbackId
}

// ===========================================================================
// decodeSendDataCallback (REQUEST type, command 0x13)
// ===========================================================================

TEST(HostApi, DecodeSendDataCallbackOk)
{
    const auto bytes    = buildRequestFrame(HostApi::CMD_SEND_DATA, {0x42, HostApi::TRANSMIT_COMPLETE_OK});
    const auto frame    = frameFromBytes(bytes);
    const auto callback = HostApi::decodeSendDataCallback(frame);
    ASSERT_TRUE(callback.has_value());
    EXPECT_EQ(callback->callbackId, 0x42);
    EXPECT_EQ(callback->txStatus, HostApi::TRANSMIT_COMPLETE_OK);
}

TEST(HostApi, DecodeSendDataCallbackNoAck)
{
    const auto bytes    = buildRequestFrame(HostApi::CMD_SEND_DATA, {0x99, HostApi::TRANSMIT_COMPLETE_NO_ACK});
    const auto frame    = frameFromBytes(bytes);
    const auto callback = HostApi::decodeSendDataCallback(frame);
    ASSERT_TRUE(callback.has_value());
    EXPECT_EQ(callback->txStatus, HostApi::TRANSMIT_COMPLETE_NO_ACK);
}

TEST(HostApi, DecodeSendDataCallbackRejectsWrongCommand)
{
    const auto bytes = buildRequestFrame(HostApi::CMD_ADD_NODE_TO_NETWORK, {0x42, 0x00, 0x00});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeSendDataCallback(frame).has_value());
}

// ===========================================================================
// decodeApplicationCommand (FUNC_ID_APPLICATION_COMMAND_HANDLER = 0x04)
// ===========================================================================

TEST(HostApi, DecodeApplicationCommand)
{
    // rxStatus=0, sourceNodeId=11, ccLen=3, ccData={0x25, 0x03, 0xFF}
    const auto bytes = buildRequestFrame(HostApi::CMD_APPLICATION_COMMAND, {0x00, 0x0B, 0x03, 0x25, 0x03, 0xFF});
    const auto frame = frameFromBytes(bytes);
    const auto cmd   = HostApi::decodeApplicationCommand(frame);
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->rxStatus, 0x00);
    EXPECT_EQ(cmd->sourceNodeId, 0x0B);
    const std::vector<std::uint8_t> expectedCc{0x25, 0x03, 0xFF};
    EXPECT_EQ(cmd->ccData, expectedCc);
}

TEST(HostApi, DecodeApplicationCommandRejectsZeroLength)
{
    const auto bytes = buildRequestFrame(HostApi::CMD_APPLICATION_COMMAND, {0x00, 0x0B, 0x00});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeApplicationCommand(frame).has_value());
}

// ===========================================================================
// decodeVersion (FUNC_ID_GET_VERSION = 0x15)
// ===========================================================================

TEST(HostApi, DecodeVersion)
{
    // 12 ASCII bytes for the version string + 1 library-type byte.
    // "Z-Wave 6.07\0" = 12 bytes; library type 1 = static controller.
    std::vector<std::uint8_t> payload{'Z', '-', 'W', 'a', 'v', 'e', ' ', '6', '.', '0', '7', 0x00, 0x01};
    const auto bytes = buildResponseFrame(HostApi::CMD_GET_VERSION, payload);
    const auto frame = frameFromBytes(bytes);
    const auto resp  = HostApi::decodeVersion(frame);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->version, "Z-Wave 6.07");
    EXPECT_EQ(resp->libraryType, HostApi::LIBRARY_TYPE_STATIC_CONTROLLER);
}

TEST(HostApi, DecodeVersionRejectsRequestType)
{
    std::vector<std::uint8_t> payload{'Z', '-', 'W', 'a', 'v', 'e', ' ', '6', '.', '0', '7', 0x00, 0x01};
    const auto bytes = buildRequestFrame(HostApi::CMD_GET_VERSION, payload);
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeVersion(frame).has_value());
}

// ===========================================================================
// decodeMemoryId (FUNC_ID_MEMORY_GET_ID = 0x20)
// ===========================================================================

TEST(HostApi, DecodeMemoryId)
{
    const auto bytes = buildResponseFrame(HostApi::CMD_MEMORY_GET_ID, {0xD9, 0x51, 0xBA, 0xB2, 0x01});
    const auto frame = frameFromBytes(bytes);
    const auto resp  = HostApi::decodeMemoryId(frame);
    ASSERT_TRUE(resp.has_value());
    const std::array<std::uint8_t, 4> expectedHomeId{0xD9, 0x51, 0xBA, 0xB2};
    EXPECT_EQ(resp->homeId, expectedHomeId);
    EXPECT_EQ(resp->controllerNodeId, 0x01);
}

// ===========================================================================
// decodeInitData (FUNC_ID_SERIAL_API_GET_INIT_DATA = 0x02)
// ===========================================================================

TEST(HostApi, DecodeInitDataExpandsBitmap)
{
    // serialApiVersion=1, capabilities=0x08 (real-primary), bitmapLen=29.
    // Bitmap byte 0 = 0b00000010 → node 2. Bitmap byte 1 = 0b00010001 → nodes 9 and 13.
    // chipType=5, chipVersion=0.
    std::vector<std::uint8_t> payload{0x01, 0x08, 0x1D};
    payload.push_back(0b00000010);  // node 2
    payload.push_back(0b00010001);  // nodes 9 and 13
    for (int i = 0; i < 27; ++i)
    {
        payload.push_back(0x00);
    }
    payload.push_back(0x05);  // chipType
    payload.push_back(0x00);  // chipVersion

    const auto bytes = buildResponseFrame(HostApi::CMD_SERIAL_API_GET_INIT_DATA, payload);
    const auto frame = frameFromBytes(bytes);
    const auto resp  = HostApi::decodeInitData(frame);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->serialApiVersion, 0x01);
    EXPECT_EQ(resp->capabilities, 0x08);
    EXPECT_EQ(resp->chipType, 0x05);
    EXPECT_EQ(resp->chipVersion, 0x00);
    const std::vector<std::uint8_t> expectedNodes{2, 9, 13};
    EXPECT_EQ(resp->nodeIds, expectedNodes);
}

// ===========================================================================
// encodeGetNodeProtocolInfo / decodeNodeProtocolInfo (FUNC_ID = 0x41)
// ===========================================================================

TEST(HostApi, EncodeGetNodeProtocolInfoCarriesNodeId)
{
    const auto frame = HostApi::encodeGetNodeProtocolInfo(0x0B);
    EXPECT_EQ(frame.getCommand(), HostApi::CMD_GET_NODE_PROTOCOL_INFO);
    EXPECT_EQ(frame.getType(), ZwaveDataFrame::FrameType::REQUEST);
    ASSERT_EQ(frame.getPayloadSize(), 1U);
    EXPECT_EQ(frame.getPayload()[0], 0x0B);
}

TEST(HostApi, DecodeNodeProtocolInfo)
{
    // capabilities=0x93 (listening, 9.6k, version 3),
    // security=0x01, reserved=0x00, basic=0x04 (Routing Slave),
    // generic=0x10 (Binary Switch), specific=0x01 (Power Switch Binary).
    const std::vector<std::uint8_t> payload{0x93, 0x01, 0x00, 0x04, 0x10, 0x01};
    const auto bytes = buildResponseFrame(HostApi::CMD_GET_NODE_PROTOCOL_INFO, payload);
    const auto frame = frameFromBytes(bytes);
    const auto resp  = HostApi::decodeNodeProtocolInfo(frame);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->capabilities, 0x93);
    EXPECT_EQ(resp->security, 0x01);
    EXPECT_EQ(resp->reserved, 0x00);
    EXPECT_EQ(resp->basicDeviceType, 0x04);
    EXPECT_EQ(resp->genericDeviceType, 0x10);
    EXPECT_EQ(resp->specificDeviceType, 0x01);
}

TEST(HostApi, DecodeNodeProtocolInfoRejectsRequestType)
{
    const auto bytes = buildRequestFrame(HostApi::CMD_GET_NODE_PROTOCOL_INFO, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeNodeProtocolInfo(frame).has_value());
}

TEST(HostApi, DecodeNodeProtocolInfoRejectsShortPayload)
{
    // Five bytes — one short of the spec minimum.
    const auto bytes = buildResponseFrame(HostApi::CMD_GET_NODE_PROTOCOL_INFO, {0x00, 0x00, 0x00, 0x00, 0x00});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeNodeProtocolInfo(frame).has_value());
}

TEST(HostApi, DecodeNodeProtocolInfoRejectsWrongCommand)
{
    const auto bytes = buildResponseFrame(HostApi::CMD_GET_VERSION, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeNodeProtocolInfo(frame).has_value());
}

// ===========================================================================
// decodeNodeCallback (FUNC_ID_ZW_ADD_NODE_TO_NETWORK / REMOVE)
// ===========================================================================

TEST(HostApi, DecodeNodeCallbackInclusionWithCcList)
{
    // sessionId=0x42, status=0x03 (ongoing — end node), nodeId=11,
    // bLen=6 (basic+generic+specific + 3 CCs),
    // basic=0x04, generic=0x10, specific=0x01,
    // CCs = {0x5E, 0x25, 0x85}
    const auto bytes    = buildRequestFrame(HostApi::CMD_ADD_NODE_TO_NETWORK,
                                            {0x42, 0x03, 0x0B, 0x06, 0x04, 0x10, 0x01, 0x5E, 0x25, 0x85});
    const auto frame    = frameFromBytes(bytes);
    const auto callback = HostApi::decodeNodeCallback(frame);
    ASSERT_TRUE(callback.has_value());
    EXPECT_EQ(callback->commandId, HostApi::CMD_ADD_NODE_TO_NETWORK);
    EXPECT_EQ(callback->sessionId, 0x42);
    EXPECT_EQ(callback->status, 0x03);
    EXPECT_EQ(callback->nodeId, 0x0B);
    EXPECT_EQ(callback->basicDeviceType, 0x04);
    EXPECT_EQ(callback->genericDeviceType, 0x10);
    EXPECT_EQ(callback->specificDeviceType, 0x01);
    const std::vector<std::uint8_t> expectedCcs{0x5E, 0x25, 0x85};
    EXPECT_EQ(callback->commandClasses, expectedCcs);
}

TEST(HostApi, DecodeNodeCallbackThinTerminalStatus)
{
    // Terminal-status callback often carries only sessionId+status+nodeId,
    // without a bLen / device-class / CC tail.
    const auto bytes    = buildRequestFrame(HostApi::CMD_ADD_NODE_TO_NETWORK, {0x42, 0x06, 0x0B});
    const auto frame    = frameFromBytes(bytes);
    const auto callback = HostApi::decodeNodeCallback(frame);
    ASSERT_TRUE(callback.has_value());
    EXPECT_EQ(callback->status, 0x06);
    EXPECT_EQ(callback->nodeId, 0x0B);
    EXPECT_TRUE(callback->commandClasses.empty());
}

TEST(HostApi, DecodeNodeCallbackRejectsUnrelatedCommand)
{
    const auto bytes = buildRequestFrame(HostApi::CMD_SEND_DATA, {0x42, 0x00, 0x00});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeNodeCallback(frame).has_value());
}

// ===========================================================================
// decodeRemoveFailedNodeResponse / decodeRemoveFailedNodeCallback
// ===========================================================================

TEST(HostApi, DecodeRemoveFailedNodeResponseStarted)
{
    const auto bytes =
        buildResponseFrame(HostApi::CMD_REMOVE_FAILED_NODE_ID, {HostApi::RemoveFailedNodeResponse::STATUS_STARTED});
    const auto frame = frameFromBytes(bytes);
    const auto resp  = HostApi::decodeRemoveFailedNodeResponse(frame);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->status, HostApi::RemoveFailedNodeResponse::STATUS_STARTED);
}

TEST(HostApi, DecodeRemoveFailedNodeResponseRejectsRequestType)
{
    // The response decoder must distinguish from the callback frame
    // (same command id, but REQUEST type).
    const auto bytes = buildRequestFrame(HostApi::CMD_REMOVE_FAILED_NODE_ID, {0x42, 0x01});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeRemoveFailedNodeResponse(frame).has_value());
}

TEST(HostApi, DecodeRemoveFailedNodeCallbackRemoved)
{
    const auto bytes    = buildRequestFrame(HostApi::CMD_REMOVE_FAILED_NODE_ID,
                                            {0x42, HostApi::RemoveFailedNodeCallback::STATUS_REMOVED});
    const auto frame    = frameFromBytes(bytes);
    const auto callback = HostApi::decodeRemoveFailedNodeCallback(frame);
    ASSERT_TRUE(callback.has_value());
    EXPECT_EQ(callback->sessionId, 0x42);
    EXPECT_EQ(callback->status, HostApi::RemoveFailedNodeCallback::STATUS_REMOVED);
}

TEST(HostApi, DecodeRemoveFailedNodeCallbackRejectsResponseType)
{
    const auto bytes = buildResponseFrame(HostApi::CMD_REMOVE_FAILED_NODE_ID, {0x42, 0x01});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeRemoveFailedNodeCallback(frame).has_value());
}

// ===========================================================================
// encodeRequestNodeInfo + decodeRequestNodeInfoResponse
// (FUNC_ID_ZW_REQUEST_NODE_INFO = 0x60)
// ===========================================================================

TEST(HostApi, EncodeRequestNodeInfo)
{
    // Wire frame carries only the nodeId; sessionId is a daemon-side
    // correlation key and never goes over the wire.
    HostApi::RequestNodeInfoRequest req{};
    req.nodeId       = 0x0C;
    req.sessionId    = 0x42;
    const auto frame = HostApi::encodeRequestNodeInfo(req);
    EXPECT_EQ(frame.getCommand(), HostApi::CMD_REQUEST_NODE_INFO);
    ASSERT_GE(frame.getPayloadSize(), 1);
    EXPECT_EQ(frame.getPayload()[0], 0x0C);
}

TEST(HostApi, DecodeRequestNodeInfoResponseAccepted)
{
    // Non-zero byte = dongle accepted the request.
    const auto bytes = buildResponseFrame(HostApi::CMD_REQUEST_NODE_INFO, {0x01});
    const auto frame = frameFromBytes(bytes);
    const auto resp  = HostApi::decodeRequestNodeInfoResponse(frame);
    ASSERT_TRUE(resp.has_value());
    EXPECT_TRUE(resp->accepted);
}

TEST(HostApi, DecodeRequestNodeInfoResponseRejected)
{
    const auto bytes = buildResponseFrame(HostApi::CMD_REQUEST_NODE_INFO, {0x00});
    const auto frame = frameFromBytes(bytes);
    const auto resp  = HostApi::decodeRequestNodeInfoResponse(frame);
    ASSERT_TRUE(resp.has_value());
    EXPECT_FALSE(resp->accepted);
}

TEST(HostApi, DecodeRequestNodeInfoResponseRejectsRequestType)
{
    // APPLICATION_UPDATE shares no FUNC_ID with REQUEST_NODE_INFO, so
    // there's no callback-frame confusion to guard against here — but
    // double-check the type discriminant anyway.
    const auto bytes = buildRequestFrame(HostApi::CMD_REQUEST_NODE_INFO, {0x01});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeRequestNodeInfoResponse(frame).has_value());
}

// ===========================================================================
// decodeApplicationUpdate (FUNC_ID_APPLICATION_UPDATE = 0x49)
// ===========================================================================

TEST(HostApi, DecodeApplicationUpdateNodeInfoReceived)
{
    // status + nodeId + length=06 + basic + generic + specific +
    // three CC bytes (CC_BASIC, CC_BINARY_SWITCH, CC_BATTERY).
    const auto bytes = buildRequestFrame(
        HostApi::CMD_APPLICATION_UPDATE,
        {HostApi::ApplicationUpdate::STATUS_NODE_INFO_RECEIVED, 0x0C, 0x06, 0x04, 0x10, 0x01, 0x20, 0x25, 0x80});
    const auto frame  = frameFromBytes(bytes);
    const auto update = HostApi::decodeApplicationUpdate(frame);
    ASSERT_TRUE(update.has_value());
    EXPECT_EQ(update->status, HostApi::ApplicationUpdate::STATUS_NODE_INFO_RECEIVED);
    EXPECT_EQ(update->nodeId, 0x0C);
    EXPECT_EQ(update->basicDeviceType, 0x04);
    EXPECT_EQ(update->genericDeviceType, 0x10);
    EXPECT_EQ(update->specificDeviceType, 0x01);
    const std::vector<std::uint8_t> expectedCcs{0x20, 0x25, 0x80};
    EXPECT_EQ(update->commandClasses, expectedCcs);
}

TEST(HostApi, DecodeApplicationUpdateReqFailedLeavesPayloadEmpty)
{
    // REQ_FAILED typically carries length=0 — only status and nodeId
    // are reliable. Decoder must not surface garbage device-class
    // bytes for this case.
    const auto bytes  = buildRequestFrame(HostApi::CMD_APPLICATION_UPDATE,
                                          {HostApi::ApplicationUpdate::STATUS_NODE_INFO_REQ_FAILED, 0x0C, 0x00});
    const auto frame  = frameFromBytes(bytes);
    const auto update = HostApi::decodeApplicationUpdate(frame);
    ASSERT_TRUE(update.has_value());
    EXPECT_EQ(update->status, HostApi::ApplicationUpdate::STATUS_NODE_INFO_REQ_FAILED);
    EXPECT_EQ(update->nodeId, 0x0C);
    EXPECT_EQ(update->basicDeviceType, 0);
    EXPECT_EQ(update->genericDeviceType, 0);
    EXPECT_EQ(update->specificDeviceType, 0);
    EXPECT_TRUE(update->commandClasses.empty());
}

TEST(HostApi, DecodeApplicationUpdateTruncatedNodeInfo)
{
    // A length byte that says "6 bytes follow" but with only 3 of
    // them present must not over-read; the decoder caps `end` at the
    // actual total. NodeInfo isn't surfaced when end < 6.
    const auto bytes  = buildRequestFrame(HostApi::CMD_APPLICATION_UPDATE,
                                          {HostApi::ApplicationUpdate::STATUS_NODE_INFO_RECEIVED, 0x0C, 0x06, 0x04});
    const auto frame  = frameFromBytes(bytes);
    const auto update = HostApi::decodeApplicationUpdate(frame);
    ASSERT_TRUE(update.has_value());
    EXPECT_EQ(update->basicDeviceType, 0);
    EXPECT_EQ(update->genericDeviceType, 0);
    EXPECT_TRUE(update->commandClasses.empty());
}

TEST(HostApi, DecodeApplicationUpdateRejectsResponseType)
{
    const auto bytes = buildResponseFrame(HostApi::CMD_APPLICATION_UPDATE,
                                          {HostApi::ApplicationUpdate::STATUS_NODE_INFO_RECEIVED, 0x0C, 0x00});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeApplicationUpdate(frame).has_value());
}

TEST(HostApi, DecodeApplicationUpdateRejectsWrongCommand)
{
    const auto bytes = buildRequestFrame(0x4A, {0x84, 0x0C, 0x00});
    const auto frame = frameFromBytes(bytes);
    EXPECT_FALSE(HostApi::decodeApplicationUpdate(frame).has_value());
}
