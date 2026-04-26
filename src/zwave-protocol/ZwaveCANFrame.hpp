#ifndef ZWAVED_ZWAVE_CAN_FRAME_HPP
#define ZWAVED_ZWAVE_CAN_FRAME_HPP

#include <cstdint>
#include <cstddef>

/**
 * Z-Wave CAN (Cancel) Frame
 * Simple frame consisting of a single CAN byte (0x18)
 * Used to abort transmission and cancel the current frame exchange
 */
class ZwaveCANFrame
{
public:
    // CAN frame byte
    static constexpr uint8_t CAN = 0x18;

    ZwaveCANFrame() = default;
    ~ZwaveCANFrame() = default;

    /**
     * Parse a CAN frame from a buffer
     * @param buffer Raw buffer containing CAN byte
     * @param length Length of buffer (must be at least 1)
     * @return true if valid CAN frame was parsed, false otherwise
     */
    [[nodiscard]] bool parseFromBuffer(const uint8_t* buffer, size_t length);

    /**
     * Get the CAN byte
     * @return CAN byte (0x18)
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

#endif // ZWAVED_ZWAVE_CAN_FRAME_HPP


