#include "SerialPort.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <utility>

#include <fcntl.h>
#include <poll.h>       // NOLINT(misc-include-cleaner): provides pollfd, POLLIN, POLLOUT, ::poll
#include <sys/types.h>  // NOLINT(misc-include-cleaner): provides ssize_t
#include <termios.h>
#include <unistd.h>

namespace
{
constexpr int INVALID_FD            = -1;
constexpr int WRITE_POLL_TIMEOUT_MS = 1000;

auto configureTermios(const int descriptor) -> bool
{
    termios tio = {};
    if (tcgetattr(descriptor, &tio) != 0)
    {
        std::cerr << "[SerialPort] tcgetattr failed: " << strerror(errno) << '\n';
        return false;
    }

    cfmakeraw(&tio);
    if (cfsetispeed(&tio, B115200) != 0 || cfsetospeed(&tio, B115200) != 0)
    {
        std::cerr << "[SerialPort] cfsetispeed/cfsetospeed failed: " << strerror(errno) << '\n';
        return false;
    }

    tio.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
    tio.c_cflag &= static_cast<tcflag_t>(~PARENB);  // no parity
    tio.c_cflag &= static_cast<tcflag_t>(~CSTOPB);  // 1 stop bit
    tio.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tio.c_cflag |= static_cast<tcflag_t>(CS8);  // 8 data bits
    tio.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);

    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(descriptor, TCSANOW, &tio) != 0)
    {
        std::cerr << "[SerialPort] tcsetattr failed: " << strerror(errno) << '\n';
        return false;
    }

    return true;
}
}  // namespace

SerialPort::~SerialPort()
{
    close();
}

SerialPort::SerialPort(SerialPort&& other) noexcept
    : descriptor(std::exchange(other.descriptor, INVALID_FD)),
      devicePath(std::move(other.devicePath))
{
}

auto SerialPort::operator=(SerialPort&& other) noexcept -> SerialPort&
{
    if (this != &other)
    {
        close();
        descriptor = std::exchange(other.descriptor, INVALID_FD);
        devicePath = std::move(other.devicePath);
    }
    return *this;
}

auto SerialPort::open(const std::string& path) -> bool
{
    close();

    int const newFd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);  // NOLINT(*-vararg)
    if (newFd < 0)
    {
        std::cerr << "[SerialPort] open(" << path << ") failed: " << strerror(errno) << '\n';
        return false;
    }

    if (!configureTermios(newFd))
    {
        ::close(newFd);
        return false;
    }

    descriptor = newFd;
    devicePath = path;
    std::cout << "[SerialPort] opened " << path << " at 115200 8N1\n";
    return true;
}

auto SerialPort::adoptFd(const int newFd, std::string label) -> void
{
    close();

    // Match the non-blocking semantics `open()` sets up via the
    // O_NONBLOCK open flag — readBytes() / writeAll() rely on
    // EAGAIN behaviour to drive their poll-based timeouts.
    if (const int flags = fcntl(newFd, F_GETFL, 0); flags >= 0)
    {
        fcntl(newFd, F_SETFL, flags | O_NONBLOCK);  // NOLINT(*-vararg)
    }

    descriptor = newFd;
    devicePath = std::move(label);
}

auto SerialPort::close() -> void
{
    if (descriptor != INVALID_FD)
    {
        ::close(descriptor);
        descriptor = INVALID_FD;
        devicePath.clear();
    }
}

auto SerialPort::isOpen() const -> bool
{
    return descriptor != INVALID_FD;
}

auto SerialPort::fd() const -> int
{
    return descriptor;
}

auto SerialPort::path() const -> const std::string&
{
    return devicePath;
}

auto SerialPort::readBytes(std::span<uint8_t> buffer, const int timeoutMs) -> int
{
    if (descriptor == INVALID_FD || buffer.empty())
    {
        return -1;
    }

    // NOLINTNEXTLINE(misc-include-cleaner): pollfd/POLLIN from <poll.h>
    pollfd pfd = {.fd = descriptor, .events = POLLIN, .revents = 0};
    // NOLINTNEXTLINE(misc-include-cleaner): poll() from <poll.h>
    int const polled = ::poll(&pfd, 1, timeoutMs);
    if (polled < 0)
    {
        if (errno == EINTR)
        {
            return 0;
        }
        std::cerr << "[SerialPort] poll failed: " << strerror(errno) << '\n';
        return -1;
    }
    if (polled == 0 || ((pfd.revents & POLLIN) == 0))
    {
        return 0;
    }

    ssize_t const got = ::read(descriptor, buffer.data(), buffer.size());
    if (got < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            return 0;
        }
        std::cerr << "[SerialPort] read failed: " << strerror(errno) << '\n';
        return -1;
    }
    return static_cast<int>(got);
}

auto SerialPort::writeAll(const std::span<const uint8_t> buffer) -> bool
{
    if (descriptor == INVALID_FD || buffer.empty())
    {
        return false;
    }

    std::size_t written = 0;
    while (written < buffer.size())
    {
        ssize_t const sent = ::write(descriptor, buffer.data() + written, buffer.size() - written);
        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // NOLINTNEXTLINE(misc-include-cleaner): pollfd/POLLOUT from <poll.h>
                pollfd pfd = {.fd = descriptor, .events = POLLOUT, .revents = 0};
                // NOLINTNEXTLINE(misc-include-cleaner): poll() from <poll.h>
                if (::poll(&pfd, 1, WRITE_POLL_TIMEOUT_MS) <= 0)
                {
                    return false;
                }
                continue;
            }
            std::cerr << "[SerialPort] write failed: " << strerror(errno) << '\n';
            return false;
        }
        written += static_cast<std::size_t>(sent);
    }
    return true;
}

auto SerialPort::flushInput() const -> void
{
    if (descriptor != INVALID_FD)
    {
        tcflush(descriptor, TCIFLUSH);
    }
}
