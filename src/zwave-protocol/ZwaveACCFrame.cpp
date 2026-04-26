#include "ZwaveACCFrame.hpp"
#include <cstdint>

bool ZwaveACCFrame::parseFromBuffer(const uint8_t* buffer, const size_t length)
{
    if (buffer == nullptr || length < 1)
    {
        valid = false;
        return false;
    }

    if (buffer[0] != ACK)
    {
        valid = false;
        return false;
    }

    valid = true;
    return true;
}

uint8_t ZwaveACCFrame::toByte()
{
    return ACK;
}

bool ZwaveACCFrame::isValid() const
{
    return valid;
}


