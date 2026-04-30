#ifndef ZWAVED_FRAME_TRANSPORT_HPP
#define ZWAVED_FRAME_TRANSPORT_HPP

#include "ZwaveDataFrame.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class SerialPort;

/**
 * Implements the Z-Wave Host API serial transport layer:
 * - Parses incoming bytes into ACK / NAK / CAN / SOF data frames.
 * - Acknowledges every well-formed inbound data frame with an ACK byte
 *   and dispatches it to the registered handler.
 * - Sends outbound request frames with retry-on-NAK/CAN/timeout
 *   per the spec's back-off (Tn = 100 + n * 1000 ms, up to 3 retries).
 *
 * Callers drive the transport by repeatedly calling pumpOnce() while
 * idle, and sendRequest() to issue commands.
 */
class FrameTransport
{
  public:
    using IncomingHandler = std::function<void(const ZwaveDataFrame&)>;

    static constexpr int ACK_TIMEOUT_MS   = 1500;
    static constexpr int MAX_SEND_RETRIES = 3;

    FrameTransport(SerialPort* port, IncomingHandler handler);

    /// Send a request data frame, retrying on NAK/CAN/timeout.
    /// Returns true if the dongle ACK'd the frame within retry budget.
    auto sendRequest(const ZwaveDataFrame& frame) -> bool;

    /// Read whatever is available without sending. Spends up to timeoutMs
    /// in a single poll, then drains any buffered bytes and dispatches any
    /// complete frames. Returns true if at least one byte was processed.
    auto pumpOnce(int timeoutMs) -> bool;

  private:
    enum class ParseState : std::uint8_t
    {
        WaitFrameStart,
        WaitLength,
        WaitPayload,
    };

    enum class TransportEvent : std::uint8_t
    {
        None,
        Ack,
        Nak,
        Can,
        DataFrame,
    };

    auto feed(uint8_t byte) -> TransportEvent;
    auto resetParser() -> void;
    auto sendAck() -> void;
    auto handleParsedDataFrame() -> void;

    SerialPort* serial;
    IncomingHandler incomingHandler;

    ParseState parseState      = ParseState::WaitFrameStart;
    std::size_t expectedLength = 0;
    std::vector<uint8_t> frameBytes;

    ZwaveDataFrame lastParsedFrame;
    bool lastParsedValid = false;
};

#endif  // ZWAVED_FRAME_TRANSPORT_HPP
