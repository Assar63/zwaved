#ifndef ZWAVED_ZWAVE_NAK_FRAME_HPP
#define ZWAVED_ZWAVE_NAK_FRAME_HPP

#include <cstddef>
#include <cstdint>

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

    ZwaveNAKFrame()                                            = default;
    ~ZwaveNAKFrame()                                           = default;
    ZwaveNAKFrame(const ZwaveNAKFrame&)                        = default;
    auto operator=(const ZwaveNAKFrame&) -> ZwaveNAKFrame&     = default;
    ZwaveNAKFrame(ZwaveNAKFrame&&) noexcept                    = default;
    auto operator=(ZwaveNAKFrame&&) noexcept -> ZwaveNAKFrame& = default;

    /**
     * Parse a NAK frame from a buffer
     * @param buffer Raw buffer containing NAK byte
     * @param length Length of buffer (must be at least 1)
     * @return true if valid NAK frame was parsed, false otherwise
     */
    [[nodiscard]] auto parseFromBuffer(const uint8_t* buffer, size_t length) -> bool;

    /**
     * Get the NAK byte
     * @return NAK byte (0x15)
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

#endif  // ZWAVED_ZWAVE_NAK_FRAME_HPP
