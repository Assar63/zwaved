#ifndef ZWAVED_ZWAVE_DATA_FRAME_HPP
#define ZWAVED_ZWAVE_DATA_FRAME_HPP

#include <vector>
#include <cstdint>

class ZwaveDataFrame
{
public:
    // Frame constants
    static constexpr uint8_t SOF = 0x01; // Start of Frame marker
    static constexpr size_t MIN_FRAME_SIZE = 5; // SOF + Length + Type + Cmd + Checksum
    static constexpr size_t MAX_PAYLOAD_SIZE = 250; // Maximum payload bytes
    static constexpr size_t MAX_FRAME_SIZE = MAX_PAYLOAD_SIZE + 5; // Full frame size

    // Frame Types
    enum class FrameType : uint8_t
    {
        REQUEST = 0x00,
        RESPONSE = 0x01,
    };

    ZwaveDataFrame() = default;
    ~ZwaveDataFrame() = default;

    /**
     * Parse a Z-Wave frame from a buffer
     * @param buffer Raw buffer containing frame data
     * @param length Length of buffer
     * @return true if frame was successfully parsed, false otherwise
     */
    bool parseFromBuffer(const uint8_t* buffer, size_t length);

    /**
     * Create a raw buffer from the frame data
     * @return Vector containing the complete frame with SOF, length, type, cmd, payload, and checksum
     */
    std::vector<uint8_t> toBuffer() const;

    /**
     * Set the frame type and command
     * @param type Frame type (REQUEST or RESPONSE)
     * @param cmd ZWave API command ID
     */
    void setHeader(FrameType type, uint8_t cmd);

    /**
     * Set the payload data
     * @param data Payload bytes
     * @param size Number of payload bytes
     * @return true if payload was set successfully, false if too large
     */
    bool setPayload(const uint8_t* data, size_t size);

    /**
     * Get the frame type
     * @return Frame type
     */
    [[nodiscard]] FrameType getType() const;

    /**
     * Get the ZWave API command ID
     * @return Command ID
     */
    [[nodiscard]] uint8_t getCommand() const;

    /**
     * Get the payload data
     * @return Pointer to payload bytes
     */
    [[nodiscard]] const uint8_t* getPayload() const;

    /**
     * Get the payload size
     * @return Number of payload bytes
     */
    [[nodiscard]] size_t getPayloadSize() const;

    /**
     * Check if frame is valid (has been properly parsed or set)
     * @return true if frame contains valid data
     */
    [[nodiscard]] bool isValid() const;

private:
    // Frame components
    FrameType frameType = FrameType::REQUEST;
    uint8_t commandId = 0;
    std::vector<uint8_t> payload;
    bool valid = false;

    /**
     * Calculate checksum for frame data
     * Checksum is XOR of all bytes from length through last payload byte
     * @return Calculated checksum
     */
    [[nodiscard]] uint8_t calculateChecksum() const;

    /**
     * Validate frame checksum
     * @param checksum Checksum from frame
     * @return true if checksum is valid
     */
    [[nodiscard]] bool validateChecksum(uint8_t checksum) const;
};

#endif // ZWAVED_ZWAVE_DATA_FRAME_HPP

