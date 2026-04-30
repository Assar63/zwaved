#include "ZwaveDataFrame.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

auto ZwaveDataFrame::parseFromBuffer(const uint8_t* buffer, const size_t length) -> bool
{
    // Check minimum size
    if (buffer == nullptr || length < MIN_FRAME_SIZE)
    {
        valid = false;
        return false;
    }

    // Check SOF marker
    if (buffer[0] != SOF)
    {
        valid = false;
        return false;
    }

    // Get frame length: number of bytes from Type through Checksum (inclusive),
    // i.e. Type + Cmd + payload + Checksum. INS12350 §6.1.
    const uint8_t frameLength = buffer[1];

    // Validate frame length: minimum is 3 (Type + Cmd + Checksum, no payload),
    // maximum is MAX_PAYLOAD_SIZE + 3.
    if (frameLength < 3 || frameLength > MAX_PAYLOAD_SIZE + 3)
    {
        valid = false;
        return false;
    }

    // Check that buffer contains complete frame: SOF + LEN-byte + frameLength bytes.
    if (length < static_cast<size_t>(frameLength) + 2)
    {
        valid = false;
        return false;
    }

    // Extract frame type
    const uint8_t typeRaw = buffer[2];
    if (typeRaw > static_cast<uint8_t>(FrameType::RESPONSE))
    {
        valid = false;
        return false;
    }
    frameType = static_cast<FrameType>(typeRaw);

    // Extract command ID
    commandId = buffer[3];

    // Extract payload: frameLength minus Type, Cmd and Checksum.
    const size_t payloadSize = frameLength - 3;
    payload.clear();
    if (payloadSize > 0)
    {
        payload.insert(payload.end(), buffer + 4, buffer + 4 + payloadSize);
    }

    // Checksum is the last byte covered by frameLength.
    const uint8_t receivedChecksum = buffer[1 + frameLength];
    if (!validateChecksum(receivedChecksum))
    {
        valid = false;
        return false;
    }

    valid = true;
    return true;
}

auto ZwaveDataFrame::toBuffer() const -> std::vector<uint8_t>
{
    std::vector<uint8_t> buffer;

    // Frame length = Type + Cmd + Payload + Checksum
    const auto frameLength = static_cast<uint8_t>(2 + payload.size() + 1);

    // Add SOF
    buffer.push_back(SOF);

    // Add Length
    buffer.push_back(frameLength);

    // Add Type
    buffer.push_back(static_cast<uint8_t>(frameType));

    // Add Command
    buffer.push_back(commandId);

    // Add Payload
    buffer.insert(buffer.end(), payload.begin(), payload.end());

    // Calculate and add Checksum
    buffer.push_back(calculateChecksum());

    return buffer;
}

auto ZwaveDataFrame::setHeader(const FrameType type, const uint8_t cmd) -> void
{
    frameType = type;
    commandId = cmd;
    valid     = true;
}

auto ZwaveDataFrame::setPayload(const uint8_t* data, const size_t size) -> bool
{
    if (size > MAX_PAYLOAD_SIZE)
    {
        return false;
    }

    payload.clear();
    if (data != nullptr && size > 0)
    {
        payload.insert(payload.end(), data, data + size);
    }

    valid = true;
    return true;
}

auto ZwaveDataFrame::getType() const -> FrameType
{
    return frameType;
}

auto ZwaveDataFrame::getCommand() const -> uint8_t
{
    return commandId;
}

auto ZwaveDataFrame::getPayload() const -> const uint8_t*
{
    if (payload.empty())
    {
        return nullptr;
    }
    return payload.data();
}

auto ZwaveDataFrame::getPayloadSize() const -> size_t
{
    return payload.size();
}

auto ZwaveDataFrame::isValid() const -> bool
{
    return valid;
}

auto ZwaveDataFrame::calculateChecksum() const -> uint8_t
{
    // Z-Wave Serial API checksum: initial register is CHECKSUM_SEED (0xFF),
    // then XOR every byte from LEN (inclusive) through the last data byte
    // (i.e. excluding SOF and the checksum byte itself). Spec: INS12350 §6.4.
    uint8_t checksum = CHECKSUM_SEED;

    // XOR with frame length
    const auto frameLength = static_cast<uint8_t>(2 + payload.size() + 1);
    checksum ^= frameLength;

    // XOR with frame type
    checksum ^= static_cast<uint8_t>(frameType);

    // XOR with command
    checksum ^= commandId;

    // XOR with payload bytes
    for (const auto byte : payload)
    {
        checksum ^= byte;
    }

    return checksum;
}

auto ZwaveDataFrame::validateChecksum(const uint8_t checksum) const -> bool
{
    return checksum == calculateChecksum();
}
