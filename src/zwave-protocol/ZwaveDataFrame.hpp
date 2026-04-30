#ifndef ZWAVED_ZWAVE_DATA_FRAME_HPP
#define ZWAVED_ZWAVE_DATA_FRAME_HPP

#include <cstdint>
#include <vector>

class ZwaveDataFrame
{
  public:
    // Frame constants
    static constexpr uint8_t SOF             = 0x01;  // Start of Frame marker
    static constexpr uint8_t CHECKSUM_SEED   = 0xFF;  // Initial value for checksum register (INS12350 §6.4)
    static constexpr size_t MIN_FRAME_SIZE   = 5;     // SOF + Length + Type + Cmd + Checksum
    static constexpr size_t MAX_PAYLOAD_SIZE = 250;   // Maximum payload bytes
    static constexpr size_t MAX_FRAME_SIZE   = MAX_PAYLOAD_SIZE + 5;  // Full frame size

    // Frame Types
    enum class FrameType : uint8_t
    {
        REQUEST  = 0x00,
        RESPONSE = 0x01,
    };

    ZwaveDataFrame()                                             = default;
    ~ZwaveDataFrame()                                            = default;
    ZwaveDataFrame(const ZwaveDataFrame&)                        = default;
    auto operator=(const ZwaveDataFrame&) -> ZwaveDataFrame&     = default;
    ZwaveDataFrame(ZwaveDataFrame&&) noexcept                    = default;
    auto operator=(ZwaveDataFrame&&) noexcept -> ZwaveDataFrame& = default;

    /**
     * Parse a Z-Wave frame from a buffer
     * @param buffer Raw buffer containing frame data
     * @param length Length of buffer
     * @return true if frame was successfully parsed, false otherwise
     */
    auto parseFromBuffer(const uint8_t* buffer, size_t length) -> bool;

    /**
     * Create a raw buffer from the frame data
     * @return Vector containing the complete frame with SOF, length, type, cmd, payload, and checksum
     */
    [[nodiscard]] auto toBuffer() const -> std::vector<uint8_t>;

    /**
     * Set the frame type and command
     * @param type Frame type (REQUEST or RESPONSE)
     * @param cmd ZWave API command ID
     */
    auto setHeader(FrameType type, uint8_t cmd) -> void;

    /**
     * Set the payload data
     * @param data Payload bytes
     * @param size Number of payload bytes
     * @return true if payload was set successfully, false if too large
     */
    auto setPayload(const uint8_t* data, size_t size) -> bool;

    /**
     * Get the frame type
     * @return Frame type
     */
    [[nodiscard]] auto getType() const -> FrameType;

    /**
     * Get the ZWave API command ID
     * @return Command ID
     */
    [[nodiscard]] auto getCommand() const -> uint8_t;

    /**
     * Get the payload data
     * @return Pointer to payload bytes
     */
    [[nodiscard]] auto getPayload() const -> const uint8_t*;

    /**
     * Get the payload size
     * @return Number of payload bytes
     */
    [[nodiscard]] auto getPayloadSize() const -> size_t;

    /**
     * Check if frame is valid (has been properly parsed or set)
     * @return true if frame contains valid data
     */
    [[nodiscard]] auto isValid() const -> bool;

  private:
    // Frame components
    FrameType frameType = FrameType::REQUEST;
    uint8_t commandId   = 0;
    std::vector<uint8_t> payload;
    bool valid = false;

    /**
     * Calculate checksum for frame data
     * Checksum is XOR of all bytes from length through last payload byte
     * @return Calculated checksum
     */
    [[nodiscard]] auto calculateChecksum() const -> uint8_t;

    /**
     * Validate frame checksum
     * @param checksum Checksum from frame
     * @return true if checksum is valid
     */
    [[nodiscard]] auto validateChecksum(uint8_t checksum) const -> bool;
};

#endif  // ZWAVED_ZWAVE_DATA_FRAME_HPP
