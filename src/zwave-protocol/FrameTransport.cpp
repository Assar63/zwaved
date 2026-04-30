#include "FrameTransport.hpp"

#include "SerialPort.hpp"
#include "ZwaveACCFrame.hpp"
#include "ZwaveCANFrame.hpp"
#include "ZwaveDataFrame.hpp"
#include "ZwaveNAKFrame.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t READ_CHUNK_SIZE      = 64;
constexpr int RETRY_BACKOFF_BASE_MS        = 100;
constexpr int RETRY_BACKOFF_PER_ATTEMPT_MS = 1000;

using Clock        = std::chrono::steady_clock;
using TimePoint    = Clock::time_point;
using Milliseconds = std::chrono::milliseconds;

auto remainingMs(const TimePoint deadline) -> int
{
    auto const now = Clock::now();
    if (now >= deadline)
    {
        return 0;
    }
    return static_cast<int>(std::chrono::duration_cast<Milliseconds>(deadline - now).count());
}

auto writeAck(SerialPort& port) -> void
{
    constexpr std::array<uint8_t, 1> ackBytes = {ZwaveACCFrame::ACK};
    if (!port.writeAll(std::span<const uint8_t>(ackBytes)))
    {
        std::cerr << "[FrameTransport] failed to write ACK\n";
    }
}
}  // namespace

FrameTransport::FrameTransport(SerialPort* port, IncomingHandler handler)
    : serial(port),
      incomingHandler(std::move(handler))
{
    frameBytes.reserve(ZwaveDataFrame::MAX_FRAME_SIZE);
}

auto FrameTransport::resetParser() -> void
{
    parseState     = ParseState::WaitFrameStart;
    expectedLength = 0;
    frameBytes.clear();
    lastParsedValid = false;
}

auto FrameTransport::feed(const uint8_t byte) -> TransportEvent
{
    switch (parseState)
    {
    case ParseState::WaitFrameStart:
    {
        if (byte == ZwaveACCFrame::ACK)
        {
            return TransportEvent::Ack;
        }
        if (byte == ZwaveNAKFrame::NAK)
        {
            return TransportEvent::Nak;
        }
        if (byte == ZwaveCANFrame::CAN)
        {
            return TransportEvent::Can;
        }
        if (byte == ZwaveDataFrame::SOF)
        {
            frameBytes.clear();
            frameBytes.push_back(byte);
            parseState = ParseState::WaitLength;
        }
        // Anything else is junk; stay in WaitFrameStart.
        return TransportEvent::None;
    }
    case ParseState::WaitLength:
    {
        frameBytes.push_back(byte);
        // Total frame size = SOF + Length + (Length bytes covering Type..Checksum).
        expectedLength = 2U + static_cast<std::size_t>(byte);
        if (byte < 3 || expectedLength > ZwaveDataFrame::MAX_FRAME_SIZE)
        {
            std::cerr << "[FrameTransport] invalid frame length " << static_cast<int>(byte) << ", resetting\n";
            resetParser();
            return TransportEvent::None;
        }
        parseState = ParseState::WaitPayload;
        return TransportEvent::None;
    }
    case ParseState::WaitPayload:
    {
        frameBytes.push_back(byte);
        if (frameBytes.size() < expectedLength)
        {
            return TransportEvent::None;
        }
        ZwaveDataFrame parsed;
        bool const parseOk = parsed.parseFromBuffer(frameBytes.data(), frameBytes.size());
        resetParser();
        if (!parseOk)
        {
            std::cerr << "[FrameTransport] data frame failed to parse (bad checksum?)\n";
            return TransportEvent::None;
        }
        lastParsedFrame = std::move(parsed);
        lastParsedValid = true;
        return TransportEvent::DataFrame;
    }
    }
    return TransportEvent::None;
}

auto FrameTransport::sendAck() -> void
{
    writeAck(*serial);
}

auto FrameTransport::handleParsedDataFrame() -> void
{
    if (!lastParsedValid)
    {
        return;
    }
    sendAck();
    if (incomingHandler)
    {
        incomingHandler(lastParsedFrame);
    }
    lastParsedValid = false;
}

auto FrameTransport::pumpOnce(const int timeoutMs) -> bool
{
    std::array<uint8_t, READ_CHUNK_SIZE> buffer{};
    int const got = serial->readBytes(std::span<uint8_t>(buffer), timeoutMs);
    if (got <= 0)
    {
        return false;
    }
    for (int i = 0; i < got; ++i)
    {
        TransportEvent const event = feed(buffer.at(static_cast<std::size_t>(i)));
        switch (event)
        {
        case TransportEvent::DataFrame:
            handleParsedDataFrame();
            break;
        case TransportEvent::Ack:
        case TransportEvent::Nak:
        case TransportEvent::Can:
            // Outside an active send these are unexpected; log and drop.
            std::cerr << "[FrameTransport] stray ACK/NAK/CAN byte while idle\n";
            break;
        case TransportEvent::None:
            break;
        }
    }
    return true;
}

auto FrameTransport::sendRequest(const ZwaveDataFrame& frame) -> bool
{
    std::vector<uint8_t> const bytes = frame.toBuffer();
    if (bytes.empty())
    {
        return false;
    }

    for (int attempt = 0; attempt < MAX_SEND_RETRIES; ++attempt)
    {
        if (attempt > 0)
        {
            int const backoff = RETRY_BACKOFF_BASE_MS + (attempt * RETRY_BACKOFF_PER_ATTEMPT_MS);
            std::this_thread::sleep_for(Milliseconds(backoff));
            serial->flushInput();
            resetParser();
        }

        if (!serial->writeAll(std::span<const uint8_t>(bytes)))
        {
            std::cerr << "[FrameTransport] write failed on attempt " << attempt + 1 << '\n';
            continue;
        }

        TimePoint const deadline = Clock::now() + Milliseconds(ACK_TIMEOUT_MS);
        bool retryOuter          = false;
        while (!retryOuter)
        {
            int const wait = remainingMs(deadline);
            if (wait <= 0)
            {
                std::cerr << "[FrameTransport] ACK timeout on attempt " << attempt + 1 << '\n';
                break;
            }

            std::array<uint8_t, READ_CHUNK_SIZE> buffer{};
            int const got = serial->readBytes(std::span<uint8_t>(buffer), wait);
            if (got <= 0)
            {
                continue;
            }

            bool ackSeen = false;
            for (int i = 0; i < got && !ackSeen && !retryOuter; ++i)
            {
                TransportEvent const event = feed(buffer.at(static_cast<std::size_t>(i)));
                switch (event)
                {
                case TransportEvent::Ack:
                    ackSeen = true;
                    break;
                case TransportEvent::Nak:
                    std::cerr << "[FrameTransport] NAK received on attempt " << attempt + 1 << '\n';
                    retryOuter = true;
                    break;
                case TransportEvent::Can:
                    std::cerr << "[FrameTransport] CAN received on attempt " << attempt + 1 << '\n';
                    retryOuter = true;
                    break;
                case TransportEvent::DataFrame:
                    handleParsedDataFrame();
                    break;
                case TransportEvent::None:
                    break;
                }
            }
            if (ackSeen)
            {
                return true;
            }
        }
    }
    std::cerr << "[FrameTransport] send failed after " << MAX_SEND_RETRIES << " attempts\n";
    return false;
}
