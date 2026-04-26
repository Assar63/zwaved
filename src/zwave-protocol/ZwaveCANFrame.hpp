#ifndef ZWAVED_ZWAVE_CAN_FRAME_HPP
#define ZWAVED_ZWAVE_CAN_FRAME_HPP

#include <cstddef>
#include <cstdint>

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

    ZwaveCANFrame()                                            = default;
    ~ZwaveCANFrame()                                           = default;
    ZwaveCANFrame(const ZwaveCANFrame&)                        = default;
    auto operator=(const ZwaveCANFrame&) -> ZwaveCANFrame&     = default;
    ZwaveCANFrame(ZwaveCANFrame&&) noexcept                    = default;
    auto operator=(ZwaveCANFrame&&) noexcept -> ZwaveCANFrame& = default;

    /**
     * Parse a CAN frame from a buffer
     * @param buffer Raw buffer containing CAN byte
     * @param length Length of buffer (must be at least 1)
     * @return true if valid CAN frame was parsed, false otherwise
     */
    [[nodiscard]] auto parseFromBuffer(const uint8_t* buffer, size_t length) -> bool;

    /**
     * Get the CAN byte
     * @return CAN byte (0x18)
     */
    [[nodiscard]] static auto toByte() -> uint8_t;

    /**
     * Check if frame is valid
     * @return true if frame is valid
     */
    [[nodiscard]] auto isValid() const -> bool;

  private:
    bool valid = false;
};

#endif  // ZWAVED_ZWAVE_CAN_FRAME_HPP
