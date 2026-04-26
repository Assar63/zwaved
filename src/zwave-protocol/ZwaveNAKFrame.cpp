#include "ZwaveNAKFrame.hpp"
#include <cstddef>
#include <cstdint>

auto ZwaveNAKFrame::parseFromBuffer(const uint8_t* buffer, const size_t length) -> bool
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

auto ZwaveNAKFrame::toByte() -> uint8_t
{
    return NAK;
}

auto ZwaveNAKFrame::isValid() const -> bool
{
    return valid;
}
