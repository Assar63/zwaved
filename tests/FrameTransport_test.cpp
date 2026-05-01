#include "FrameTransport.hpp"
#include "SerialPort.hpp"
#include "ZwaveDataFrame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
// Z-Wave Serial API single-byte transport markers (INS12350 §6).
constexpr std::uint8_t ACK_BYTE = 0x06;
constexpr std::uint8_t NAK_BYTE = 0x15;
constexpr std::uint8_t CAN_BYTE = 0x18;

/// Build a complete on-the-wire frame (SOF + length + type + cmd +
/// payload + checksum) that the test pushes into the peer side of
/// the socketpair.
auto buildFrame(ZwaveDataFrame::FrameType type,
                std::uint8_t cmd,
                const std::vector<std::uint8_t>& payload) -> std::vector<std::uint8_t>
{
    ZwaveDataFrame frame;
    frame.setHeader(type, cmd);
    frame.setPayload(payload.data(), payload.size());
    return frame.toBuffer();
}

/// Test fixture: stand up a socketpair, hand one end to a SerialPort
/// via SerialPort::adoptFd, keep the other end ("peer") for the test
/// to drive. Production read/write paths run unchanged — same
/// poll(), read(), write() syscalls — so we exercise the real
/// transport state machine.
class FrameTransportTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        std::array<int, 2> fds{-1, -1};
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()), 0);
        port.adoptFd(fds[0], "test:transport-fd");
        peerFd = fds[1];
    }

    void TearDown() override
    {
        if (peerFd >= 0)
        {
            ::close(peerFd);
            peerFd = -1;
        }
    }

    /// Block up to timeoutMs for `peerFd` to become readable. Returns
    /// true if data is ready, false on timeout.
    auto peerWait(int timeoutMs) const -> bool
    {
        pollfd pfd{.fd = peerFd, .events = POLLIN, .revents = 0};
        return ::poll(&pfd, 1, timeoutMs) > 0;
    }

    /// Read whatever is currently in the peer side's receive buffer
    /// (up to `cap` bytes). Returns the bytes read; empty on EOF /
    /// error.
    auto peerRead(std::size_t cap) const -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> bytes(cap, 0);
        const ssize_t got = ::read(peerFd, bytes.data(), bytes.size());
        if (got <= 0)
        {
            return {};
        }
        bytes.resize(static_cast<std::size_t>(got));
        return bytes;
    }

    /// Read exactly one byte from the peer with a wait. Returns
    /// nullopt on timeout.
    auto peerReadByte(int timeoutMs) const -> std::optional<std::uint8_t>
    {
        if (!peerWait(timeoutMs))
        {
            return std::nullopt;
        }
        std::uint8_t byte = 0;
        if (::read(peerFd, &byte, 1) != 1)
        {
            return std::nullopt;
        }
        return byte;
    }

    auto peerWrite(std::span<const std::uint8_t> bytes) const -> bool
    {
        const ssize_t sent = ::write(peerFd, bytes.data(), bytes.size());
        return sent == static_cast<ssize_t>(bytes.size());
    }

    SerialPort port;
    int peerFd = -1;
};

}  // namespace

// ============================================================================
// pumpOnce: passive read path
// ============================================================================

TEST_F(FrameTransportTest, PumpOnceTimesOutWithNoInput)
{
    int handlerCalls = 0;
    FrameTransport transport(&port, [&handlerCalls](const ZwaveDataFrame&) { ++handlerCalls; });
    EXPECT_FALSE(transport.pumpOnce(50));
    EXPECT_EQ(handlerCalls, 0);
}

TEST_F(FrameTransportTest, PumpOnceDispatchesCompleteFrameAndAcksPeer)
{
    int handlerCalls     = 0;
    std::uint8_t lastCmd = 0;
    FrameTransport transport(&port,
                             [&handlerCalls, &lastCmd](const ZwaveDataFrame& frame)
                             {
                                 ++handlerCalls;
                                 lastCmd = frame.getCommand();
                             });

    // Inject a SendData callback frame from the peer (REQUEST type,
    // command 0x13, payload = callbackId + txStatus).
    const auto bytes = buildFrame(ZwaveDataFrame::FrameType::REQUEST, 0x13, {0x42, 0x00});
    ASSERT_TRUE(peerWrite(std::span<const std::uint8_t>(bytes)));

    EXPECT_TRUE(transport.pumpOnce(500));
    EXPECT_EQ(handlerCalls, 1);
    EXPECT_EQ(lastCmd, 0x13);

    // FrameTransport::handleParsedDataFrame must ACK every well-formed
    // inbound frame back to the dongle.
    const auto ack = peerReadByte(500);
    ASSERT_TRUE(ack.has_value());
    EXPECT_EQ(*ack, ACK_BYTE);
}

TEST_F(FrameTransportTest, PumpOnceHandlesPartialFrameAcrossReads)
{
    int handlerCalls = 0;
    FrameTransport transport(&port, [&handlerCalls](const ZwaveDataFrame&) { ++handlerCalls; });

    const auto bytes = buildFrame(ZwaveDataFrame::FrameType::REQUEST, 0x04, {0x00, 0x0B, 0x03, 0x25, 0x03, 0xFF});

    // First half of the frame: parser stops in WaitPayload, no
    // dispatch yet.
    const std::span<const std::uint8_t> firstHalf{bytes.data(), bytes.size() / 2};
    ASSERT_TRUE(peerWrite(firstHalf));
    transport.pumpOnce(200);
    EXPECT_EQ(handlerCalls, 0);

    // Remainder completes the frame; dispatch fires.
    const std::span<const std::uint8_t> secondHalf{bytes.data() + (bytes.size() / 2),
                                                   bytes.size() - (bytes.size() / 2)};
    ASSERT_TRUE(peerWrite(secondHalf));
    EXPECT_TRUE(transport.pumpOnce(500));
    EXPECT_EQ(handlerCalls, 1);
}

TEST_F(FrameTransportTest, PumpOnceDropsBadChecksumWithoutDispatching)
{
    int handlerCalls = 0;
    FrameTransport transport(&port, [&handlerCalls](const ZwaveDataFrame&) { ++handlerCalls; });

    auto bytes = buildFrame(ZwaveDataFrame::FrameType::REQUEST, 0x13, {0x42, 0x00});
    bytes.back() ^= 0xFF;  // corrupt the checksum
    ASSERT_TRUE(peerWrite(std::span<const std::uint8_t>(bytes)));

    transport.pumpOnce(200);
    EXPECT_EQ(handlerCalls, 0);
    // No ACK emitted on bad-checksum frames.
    EXPECT_FALSE(peerWait(50));
}

// ============================================================================
// sendRequest: active write path with retries
// ============================================================================

TEST_F(FrameTransportTest, SendRequestSucceedsOnImmediateAck)
{
    FrameTransport transport(&port, [](const ZwaveDataFrame&) {});

    ZwaveDataFrame frame;
    frame.setHeader(ZwaveDataFrame::FrameType::REQUEST, 0x15);  // GET_VERSION

    // Drive sendRequest from a worker thread so the test thread can
    // synchronously answer it.
    std::thread sender([&transport, &frame] { EXPECT_TRUE(transport.sendRequest(frame)); });

    // Drain whatever the transport wrote — we don't validate the bytes
    // here; that's the encoder's job, covered by HostApi_test.
    ASSERT_TRUE(peerWait(500));
    EXPECT_GT(peerRead(64).size(), 0U);

    const std::array<std::uint8_t, 1> ackBytes{ACK_BYTE};
    ASSERT_TRUE(peerWrite(std::span<const std::uint8_t>(ackBytes)));

    sender.join();
}

TEST_F(FrameTransportTest, SendRequestRetriesAfterNak)
{
    FrameTransport transport(&port, [](const ZwaveDataFrame&) {});

    ZwaveDataFrame frame;
    frame.setHeader(ZwaveDataFrame::FrameType::REQUEST, 0x15);

    std::thread sender([&transport, &frame] { EXPECT_TRUE(transport.sendRequest(frame)); });

    // First attempt — drain the bytes, NAK them.
    ASSERT_TRUE(peerWait(500));
    EXPECT_GT(peerRead(64).size(), 0U);
    const std::array<std::uint8_t, 1> nakBytes{NAK_BYTE};
    ASSERT_TRUE(peerWrite(std::span<const std::uint8_t>(nakBytes)));

    // Second attempt arrives after the spec backoff (Tn = 100 +
    // 1*1000 ms). Drain and ACK.
    ASSERT_TRUE(peerWait(2500));
    EXPECT_GT(peerRead(64).size(), 0U);
    const std::array<std::uint8_t, 1> ackBytes{ACK_BYTE};
    ASSERT_TRUE(peerWrite(std::span<const std::uint8_t>(ackBytes)));

    sender.join();
}

TEST_F(FrameTransportTest, SendRequestRetriesAfterCan)
{
    FrameTransport transport(&port, [](const ZwaveDataFrame&) {});

    ZwaveDataFrame frame;
    frame.setHeader(ZwaveDataFrame::FrameType::REQUEST, 0x15);

    std::thread sender([&transport, &frame] { EXPECT_TRUE(transport.sendRequest(frame)); });

    ASSERT_TRUE(peerWait(500));
    EXPECT_GT(peerRead(64).size(), 0U);
    const std::array<std::uint8_t, 1> canBytes{CAN_BYTE};
    ASSERT_TRUE(peerWrite(std::span<const std::uint8_t>(canBytes)));

    ASSERT_TRUE(peerWait(2500));
    EXPECT_GT(peerRead(64).size(), 0U);
    const std::array<std::uint8_t, 1> ackBytes{ACK_BYTE};
    ASSERT_TRUE(peerWrite(std::span<const std::uint8_t>(ackBytes)));

    sender.join();
}
