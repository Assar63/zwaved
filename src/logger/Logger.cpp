#include "Logger.hpp"

#include "../zwaved.h"  // NOLINT(misc-include-cleaner): used via __attribute__ constructor priority

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
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

struct LoggerState
{
    std::thread thread;
    std::atomic<bool> running{false};
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<LogEntry> queue;
};

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
    {
        std::scoped_lock const lock(state().mutex);
        state().queue.push_back(
            LogEntry{.level = level, .timestamp = std::chrono::system_clock::now(), .message = std::move(message)});
    }
    state().cv.notify_one();
}

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

namespace
{
__attribute__((constructor(CONFIG_LOGGER_PRIO))) auto startLogger() -> void
{
    state().running = true;
    state().thread  = std::thread(loggerThread);
}

__attribute__((destructor(CONFIG_LOGGER_PRIO))) auto stopLogger() -> void
{
    {
        std::scoped_lock const lock(state().mutex);
        state().running = false;
    }
    state().cv.notify_all();
    if (state().thread.joinable())
    {
        state().thread.join();
    }
}
}  // namespace
