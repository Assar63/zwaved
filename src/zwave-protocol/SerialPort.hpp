#ifndef ZWAVED_SERIAL_PORT_HPP
#define ZWAVED_SERIAL_PORT_HPP

#include <cstdint>
#include <span>
#include <string>

/**
 * RAII wrapper around a Z-Wave dongle serial port.
 * Configures the TTY for raw 115200 8N1 (the Z-Wave Host API serial
 * link standard) and exposes blocking-with-timeout reads via poll().
 */
class SerialPort
{
  public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(const SerialPort&)                    = delete;
    auto operator=(const SerialPort&) -> SerialPort& = delete;
    SerialPort(SerialPort&& other) noexcept;
    auto operator=(SerialPort&& other) noexcept -> SerialPort&;

    /// Open the TTY at the given path and configure 115200 8N1 raw.
    /// Returns true on success, false on any failure (errno preserved).
    [[nodiscard]] auto open(const std::string& path) -> bool;

    /// Adopt an already-open file descriptor — used by unit tests that
    /// want to drive `FrameTransport` over a `socketpair(2)` instead
    /// of a real serial line. Closes any currently held fd, sets
    /// `O_NONBLOCK` on the new one (matches what `open()` configures
    /// so the poll-based read/write paths behave the same), and uses
    /// `label` as the value returned by `path()`. No termios
    /// configuration is run — sockets and pipes don't accept
    /// `tcsetattr`, and the caller is responsible for whatever the
    /// fd needs to look like before it gets here. Ownership transfers:
    /// the destructor will `close(2)` it.
    auto adoptFd(int newFd, std::string label) -> void;

    /// Close the underlying fd if open.
    auto close() -> void;

    [[nodiscard]] auto isOpen() const -> bool;
    [[nodiscard]] auto fd() const -> int;
    [[nodiscard]] auto path() const -> const std::string&;

    /// Blocking-with-timeout read into the given span.
    /// Returns the number of bytes read, 0 on timeout, -1 on error.
    auto readBytes(std::span<uint8_t> buffer, int timeoutMs) -> int;

    /// Write all bytes in the given span; returns true on success.
    auto writeAll(std::span<const uint8_t> buffer) -> bool;

    /// Drain any pending input bytes; used to recover after errors.
    auto flushInput() const -> void;

  private:
    int descriptor = -1;
    std::string devicePath;
};

#endif  // ZWAVED_SERIAL_PORT_HPP
