#include "ZwaveACCFrame.hpp"

#include <cstddef>
#include <cstdint>

auto ZwaveACCFrame::parseFromBuffer(const uint8_t* buffer, const size_t length) -> bool
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

auto ZwaveACCFrame::toByte() -> uint8_t
{
    return ACK;
}

auto ZwaveACCFrame::isValid() const -> bool
{
    return valid;
}
