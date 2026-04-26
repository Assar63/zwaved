#include "ZwaveNAKFrame.hpp"
#include <cstdint>

bool ZwaveNAKFrame::parseFromBuffer(const uint8_t* buffer, const size_t length)
{
    if (buffer == nullptr || length < 1)
    {
        valid = false;
        return false;
    }

    if (buffer[0] != NAK)
    {
        valid = false;
        return false;
    }

    valid = true;
    return true;
}

uint8_t ZwaveNAKFrame::toByte()
{
    return NAK;
}

bool ZwaveNAKFrame::isValid() const
{
    return valid;
}


