#ifndef ZWAVED_ZWAVE_NAK_FRAME_HPP
#define ZWAVED_ZWAVE_NAK_FRAME_HPP

#include <cstdint>
#include <cstddef>

/**
 * Z-Wave Negative Acknowledgement (NAK) Frame
 * Simple frame consisting of a single NAK byte (0x15)
 * Used to indicate unsuccessful reception or processing of a frame
 */
class ZwaveNAKFrame
{
public:
    // NAK frame byte
    static constexpr uint8_t NAK = 0x15;

    ZwaveNAKFrame() = default;
    ~ZwaveNAKFrame() = default;

    /**
     * Parse a NAK frame from a buffer
     * @param buffer Raw buffer containing NAK byte
     * @param length Length of buffer (must be at least 1)
     * @return true if valid NAK frame was parsed, false otherwise
     */
    [[nodiscard]] bool parseFromBuffer(const uint8_t* buffer, size_t length);

    /**
     * Get the NAK byte
     * @return NAK byte (0x15)
     */
    [[nodiscard]] static uint8_t toByte();

    /**
     * Check if frame is valid
     * @return true if frame is valid
     */
    [[nodiscard]] bool isValid() const;

private:
    bool valid = false;
};

#endif // ZWAVED_ZWAVE_NAK_FRAME_HPP


