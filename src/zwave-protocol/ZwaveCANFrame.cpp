#include "ZwaveCANFrame.hpp"

bool ZwaveCANFrame::parseFromBuffer(const uint8_t* buffer, const size_t length)
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

uint8_t ZwaveCANFrame::toByte()
{
    return CAN;
}

bool ZwaveCANFrame::isValid() const
{
    return valid;
}


