#ifndef ZWAVED_ZWAVE_ACC_FRAME_HPP
#define ZWAVED_ZWAVE_ACC_FRAME_HPP

#include <cstddef>
#include <cstdint>

/**
 * Z-Wave Acknowledgement (ACK) Frame
 * Simple frame consisting of a single ACK byte (0x06)
 * Used to acknowledge successful reception of a data frame
 */
class ZwaveACCFrame
{
  public:
    // ACK frame byte
    static constexpr uint8_t ACK = 0x06;

    ZwaveACCFrame()                                            = default;
    ~ZwaveACCFrame()                                           = default;
    ZwaveACCFrame(const ZwaveACCFrame&)                        = default;
    auto operator=(const ZwaveACCFrame&) -> ZwaveACCFrame&     = default;
    ZwaveACCFrame(ZwaveACCFrame&&) noexcept                    = default;
    auto operator=(ZwaveACCFrame&&) noexcept -> ZwaveACCFrame& = default;

    /**
     * Parse an ACK frame from a buffer
     * @param buffer Raw buffer containing ACK byte
     * @param length Length of buffer (must be at least 1)
     * @return true if valid ACK frame was parsed, false otherwise
     */
    [[nodiscard]] auto parseFromBuffer(const uint8_t* buffer, size_t length) -> bool;

    /**
     * Get the ACK byte
     * @return ACK byte (0x06)
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

#endif  // ZWAVED_ZWAVE_ACC_FRAME_HPP
