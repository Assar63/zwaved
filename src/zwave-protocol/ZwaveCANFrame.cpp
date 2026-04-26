#include "ZwaveCANFrame.hpp"
#include <cstddef>
#include <cstdint>

auto ZwaveCANFrame::parseFromBuffer(const uint8_t* buffer, const size_t length) -> bool
{
    if (buffer == nullptr || length < 1)
    {
        valid = false;
        return false;
    }

    if (buffer[0] != CAN)
    {
        valid = false;
        return false;
    }

    valid = true;
    return true;
}

auto ZwaveCANFrame::toByte() -> uint8_t
{
    return CAN;
}

auto ZwaveCANFrame::isValid() const -> bool
{
    return valid;
}
