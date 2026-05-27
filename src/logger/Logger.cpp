#include "Logger.hpp"

#include "../message-bus/MessageBus.hpp"
#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>  // NOLINT(misc-include-cleaner): errno checked in captureReaderLoop's read() error branch
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <sys/prctl.h>
#include <unistd.h>  // NOLINT(misc-include-cleaner): pipe/dup2/read/close/STDOUT_FILENO/STDERR_FILENO

#ifdef ZWAVED_LOGGER_SYSLOG
#    include <syslog.h>
#endif

namespace
{
constexpr std::size_t QUEUE_DRAIN_BATCH   = 64;
constexpr std::size_t TIMESTAMP_BUF_BYTES = 32;
constexpr int MS_PER_SECOND               = 1000;
constexpr int TIMESTAMP_FRACTIONAL_DIGITS = 3;
constexpr const char* THREAD_NAME         = "ZWaveLog";

struct LogEntry
{
    Logger::Level level;
    std::chrono::system_clock::time_point timestamp;
    std::string message;
};

/// Move-only RAII wrapper around a POSIX file descriptor. Closes on
/// destruction; `release()` hands ownership back to the caller; `reset()`
/// closes any current fd and optionally adopts a new one.
class FdHandle
{
  public:
    FdHandle() = default;
    // NOLINTNEXTLINE(readability-identifier-length): `fd` is the conventional name for a file descriptor
    explicit FdHandle(int fd)
        : fd_(fd)
    {
    }

    ~FdHandle()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
    }

    FdHandle(const FdHandle&)                    = delete;
    auto operator=(const FdHandle&) -> FdHandle& = delete;
    FdHandle(FdHandle&& other) noexcept
        : fd_(std::exchange(other.fd_, -1))
    {
    }
    auto operator=(FdHandle&& other) noexcept -> FdHandle&
    {
        if (this != &other)
        {
            if (fd_ >= 0)
            {
                ::close(fd_);
            }
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] auto get() const noexcept -> int
    {
        return fd_;
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return fd_ >= 0;
    }
    auto reset() noexcept -> void
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }
    auto release() noexcept -> int
    {
        return std::exchange(fd_, -1);
    }

  private:
    int fd_ = -1;
};

/// Per-stream capture state for stdout / stderr. The kernel duplicates
/// the writer side of a pipe over fd 1 / fd 2, so any printf or
/// std::cout in the process now writes into the pipe; a dedicated
/// reader thread drains it line-by-line into Logger::log.
struct StreamCapture
{
    std::thread thread;
    FdHandle readFd;
    FdHandle targetFd;  // STDOUT_FILENO or STDERR_FILENO, owned so closing it wakes the reader with EOF
    Logger::Level level = Logger::Level::Info;
    std::string buffer;  // partial-line accumulator, owned by the reader thread
};

// Forward decls for the destructor below; defined further down where
// the syslog capture machinery lives.
#ifdef ZWAVED_LOGGER_SYSLOG
auto joinCapture(StreamCapture& capture) -> void;
#endif

// NOLINTBEGIN(misc-non-private-member-variables-in-classes): file-local singleton, public members read like a struct
struct LoggerState
{
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<Logger::Level> minLevel{Logger::Level::Info};
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<LogEntry> queue;
    MessageBus::SubscriptionGuard loggerConfigSub;

#ifdef ZWAVED_LOGGER_SYSLOG
    std::once_flag claimFlag;
    StreamCapture stdoutCapture;
    StreamCapture stderrCapture;
#endif

    // Static-state destructor. Must run before the std::thread member
    // is destroyed — see the long-form note in ExternalApiThread.cpp.
    // Logger is constructed first (priority 101) so its atexit fires
    // *last*; by that point Protocol/Monitor/ExternalApi have already
    // joined their workers, so no producer is still calling Logger::log.
    ~LoggerState()
    {
#ifdef ZWAVED_LOGGER_SYSLOG
        // Drain the captures *before* the consumer thread, so any
        // trailing partial line each reader flushes still lands in the
        // queue while the consumer is alive to drain it.
        joinCapture(stdoutCapture);
        joinCapture(stderrCapture);
#endif
        {
            std::scoped_lock const lock(mutex);
            running = false;
        }
        cv.notify_all();
        if (thread.joinable())
        {
            thread.join();
        }
    }

    LoggerState()                                          = default;
    LoggerState(const LoggerState&)                        = delete;
    auto operator=(const LoggerState&) -> LoggerState&     = delete;
    LoggerState(LoggerState&&) noexcept                    = delete;
    auto operator=(LoggerState&&) noexcept -> LoggerState& = delete;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

auto state() -> LoggerState&
{
    static LoggerState instance;
    return instance;
}

auto levelTag(Logger::Level level) -> std::string_view
{
    switch (level)
    {
    case Logger::Level::Debug:
        return "DEBUG";
    case Logger::Level::Info:
        return "INFO ";
    case Logger::Level::Warn:
        return "WARN ";
    case Logger::Level::Error:
        return "ERROR";
    }
    return "?    ";
}

#ifdef ZWAVED_LOGGER_SYSLOG
auto levelToSyslog(Logger::Level level) -> int
{
    switch (level)
    {
    case Logger::Level::Debug:
        return LOG_DEBUG;
    case Logger::Level::Info:
        return LOG_INFO;
    case Logger::Level::Warn:
        return LOG_WARNING;
    case Logger::Level::Error:
        return LOG_ERR;
    }
    return LOG_NOTICE;
}
#endif

auto formatTimestamp(std::chrono::system_clock::time_point timestamp) -> std::string
{
    const auto epoch = std::chrono::system_clock::to_time_t(timestamp);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count() % MS_PER_SECOND;
    std::tm utc{};
    ::gmtime_r(&epoch, &utc);
    std::array<char, TIMESTAMP_BUF_BYTES> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &utc);
    std::ostringstream stream;
    stream << buffer.data() << '.' << std::setw(TIMESTAMP_FRACTIONAL_DIGITS) << std::setfill('0') << millis << 'Z';
    return stream.str();
}

/// Render an entry to the configured sink. Called from the consumer
/// thread only.
auto emit(const LogEntry& entry) -> void
{
#ifdef ZWAVED_LOGGER_SYSLOG
    // syslog(3) handles its own timestamp and ident; just hand it the
    // pre-formatted message body and severity.
    syslog(levelToSyslog(entry.level), "%s", entry.message.c_str());  // NOLINT(misc-include-cleaner)
#else
    std::cout << '[' << formatTimestamp(entry.timestamp) << "] [" << levelTag(entry.level) << "] " << entry.message
              << '\n';
    std::cout.flush();
#endif
}

auto pushEntry(Logger::Level level, std::string message) -> void
{
    // Producer-side gate. The atomic load is relaxed because the
    // exact level a single message sees doesn't have to be
    // synchronized with anything else — eventual consistency is fine
    // for log filtering.
    if (static_cast<std::uint8_t>(level) < static_cast<std::uint8_t>(state().minLevel.load(std::memory_order_relaxed)))
    {
        return;
    }
    {
        std::scoped_lock const lock(state().mutex);
        state().queue.push_back(
            LogEntry{.level = level, .timestamp = std::chrono::system_clock::now(), .message = std::move(message)});
    }
    state().cv.notify_one();
}

#ifdef ZWAVED_LOGGER_SYSLOG
constexpr std::size_t CAPTURE_CHUNK_BYTES = 256;
constexpr const char* STDOUT_READER_NAME  = "ZWaveCapO";
constexpr const char* STDERR_READER_NAME  = "ZWaveCapE";

/// Reader-thread body for a captured stream. Reads in chunks, splits
/// on newlines, ships each complete line through Logger::log. Exits on
/// EOF (the daemon's destructor closes the writer-side fd as part of
/// teardown), draining any trailing partial line as it leaves.
auto captureReaderLoop(StreamCapture& capture, const char* threadName) -> void
{
    prctl(PR_SET_NAME, threadName, 0, 0, 0);  // NOLINT(misc-include-cleaner)
    std::array<char, CAPTURE_CHUNK_BYTES> chunk{};
    while (true)
    {
        const ssize_t got = ::read(capture.readFd.get(), chunk.data(), chunk.size());
        if (got > 0)
        {
            capture.buffer.append(chunk.data(), static_cast<std::size_t>(got));
            std::size_t pos = 0;
            while ((pos = capture.buffer.find('\n')) != std::string::npos)
            {
                std::string line = capture.buffer.substr(0, pos);
                capture.buffer.erase(0, pos + 1);
                if (!line.empty())
                {
                    Logger::log(capture.level, std::move(line));
                }
            }
            continue;
        }
        if (got == 0)
        {
            break;  // writer side closed → EOF
        }
        if (errno == EINTR)
        {
            continue;
        }
        break;  // any other error: stop draining
    }
    if (!capture.buffer.empty())
    {
        Logger::log(capture.level, std::move(capture.buffer));
    }
}

/// Build a pipe and dup2 its writer-side over `targetFd`, so anything
/// that writes to the original fd (printf to stdout, std::cerr, a
/// dlopen'd library that hits assert) now lands in the pipe. The
/// reader-side fd is stashed for the reader thread.
auto stealStream(StreamCapture& capture, int targetFd, Logger::Level level, const char* threadName) -> void
{
    std::array<int, 2> pipeFds{-1, -1};
    if (::pipe(pipeFds.data()) != 0)
    {
        std::cerr << "[Logger] pipe() for fd " << targetFd << " failed\n";
        return;
    }
    FdHandle readEnd(pipeFds.at(0));
    FdHandle writeEnd(pipeFds.at(1));

    if (::dup2(writeEnd.get(), targetFd) < 0)
    {
        std::cerr << "[Logger] dup2() for fd " << targetFd << " failed\n";
        return;  // both ends cleaned up by FdHandle dtors
    }
    // The original write end is no longer needed — fd `targetFd` is
    // now a duplicate. Letting writeEnd's destructor close it leaves
    // only targetFd as the surviving reference; closing targetFd in
    // joinCapture therefore reaches the reader as EOF.
    capture.readFd   = std::move(readEnd);
    capture.targetFd = FdHandle(targetFd);
    capture.level    = level;
    capture.thread   = std::thread([&capture, threadName] { captureReaderLoop(capture, threadName); });
}

auto joinCapture(StreamCapture& capture) -> void
{
    // Close the dup2'd writer side. The reader sees EOF and exits.
    capture.targetFd.reset();
    if (capture.thread.joinable())
    {
        capture.thread.join();
    }
    capture.readFd.reset();
}
#endif  // ZWAVED_LOGGER_SYSLOG

/// Consumer loop — drain the queue in batches and emit each entry.
/// `running == false` plus an empty queue ends the loop, so the
/// destructor's wake-up always exits cleanly.
auto loggerThread() -> void
{
    prctl(PR_SET_NAME, THREAD_NAME, 0, 0, 0);  // NOLINT(misc-include-cleaner)

#ifdef ZWAVED_LOGGER_SYSLOG
    // Open the syslog connection once for the lifetime of the daemon.
    // LOG_PID stamps every line; LOG_DAEMON is the standard facility
    // for system services.
    openlog("zwaved", LOG_PID, LOG_DAEMON);  // NOLINT(misc-include-cleaner)
#endif

    std::deque<LogEntry> batch;
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(state().mutex);
            state().cv.wait(lock, [] { return !state().queue.empty() || !state().running.load(); });
            // Move up to QUEUE_DRAIN_BATCH entries out under the lock,
            // then release before doing I/O so producers aren't
            // throttled by the sink.
            const std::size_t take = std::min(state().queue.size(), QUEUE_DRAIN_BATCH);
            for (std::size_t i = 0; i < take; ++i)
            {
                batch.push_back(std::move(state().queue.front()));
                state().queue.pop_front();
            }
            if (batch.empty() && !state().running.load())
            {
                break;
            }
        }
        for (auto& entry : batch)
        {
            emit(entry);
        }
        batch.clear();
    }

#ifdef ZWAVED_LOGGER_SYSLOG
    closelog();  // NOLINT(misc-include-cleaner)
#endif
}
}  // namespace

auto Logger::log(Level level, std::string message) -> void
{
    pushEntry(level, std::move(message));
}

auto Logger::debug(std::string message) -> void
{
    pushEntry(Level::Debug, std::move(message));
}

auto Logger::info(std::string message) -> void
{
    pushEntry(Level::Info, std::move(message));
}

auto Logger::warn(std::string message) -> void
{
    pushEntry(Level::Warn, std::move(message));
}

auto Logger::error(std::string message) -> void
{
    pushEntry(Level::Error, std::move(message));
}

auto Logger::setMinLevel(Level level) -> void
{
    state().minLevel.store(level, std::memory_order_relaxed);
}

auto Logger::claimStandardStreams() -> void
{
#ifdef ZWAVED_LOGGER_SYSLOG
    std::call_once(state().claimFlag,
                   []
                   {
                       // stdin is meaningless for a daemon — point it at /dev/null
                       // so any fgets() / read() returns immediately rather than
                       // blocking on a closed terminal.
                       if (std::freopen("/dev/null", "r", stdin) == nullptr)
                       {
                           std::cerr << "[Logger] freopen(stdin, /dev/null) failed\n";
                       }
                       stealStream(state().stdoutCapture, STDOUT_FILENO, Level::Info, STDOUT_READER_NAME);
                       stealStream(state().stderrCapture, STDERR_FILENO, Level::Error, STDERR_READER_NAME);
                       // Force line buffering so a half-finished printf doesn't
                       // sit in the FILE* buffer waiting for a flush.
                       ::setvbuf(stdout, nullptr, _IOLBF, 0);
                       ::setvbuf(stderr, nullptr, _IOLBF, 0);
                   });
#endif
    // Under the stdout sink: deliberate no-op (capturing stdout would
    // loop the consumer thread's own writes back through the pipe).
}

namespace
{
__attribute__((constructor(CONFIG_LOGGER_PRIO))) auto startLogger() -> void
{
    // Touch the MessageBus singleton first so its atexit handler is
    // registered before any module's. LIFO destruction then guarantees
    // the bus outlives every state that may call unsubscribe(...) from
    // a joining-thread destructor.
    MessageBus::touch();

    state().running = true;
    state().thread  = std::thread(loggerThread);

    // Subscribe to LoggerConfig now (priority 101). Config publishes at
    // priority 102, *after* this constructor returns, so the subscriber
    // is on the bus before the publish — the handler fires when Config
    // calls publish() and updates the producer-side level gate.
    state().loggerConfigSub = MessageBus::SubscriptionGuard(MessageBus::subscribe<MessageBus::LoggerConfig>(
        [](const MessageBus::LoggerConfig& cfg) -> void
        { Logger::setMinLevel(static_cast<Logger::Level>(cfg.minLevel)); }));

#ifdef ZWAVED_LOGGER_SYSLOG
    // With the syslog sink there is no looping risk and capturing
    // stdout/stderr is the whole point of running as a daemon — engage
    // it automatically so callers don't have to remember.
    Logger::claimStandardStreams();
#endif
}
// Shutdown lives in LoggerState's destructor (see comment there).
}  // namespace
