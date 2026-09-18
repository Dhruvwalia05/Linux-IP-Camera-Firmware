#include "logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <sys/syscall.h>
#include <unistd.h>

namespace
{

long CurrentThreadId()
{
    return static_cast<long>(::syscall(SYS_gettid));
}

std::string FormatTimestamp()
{
    using namespace std::chrono;

    const auto now       = system_clock::now();
    const auto time_t_now = system_clock::to_time_t(now);
    const auto millis    =
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm_buf{};
    localtime_r(&time_t_now, &tm_buf);

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.'
        << std::setfill('0') << std::setw(3) << millis;

    return oss.str();
}

} // namespace

Logger& Logger::GetInstance()
{
    static Logger instance;
    return instance;
}

Logger::Logger()
    : initialized(false)
{
}

Logger::~Logger()
{
}

LoggerError Logger::Initialize()
{
    return Initialize(LoggerConfig{});
}

LoggerError Logger::Initialize(const LoggerConfig& cfg)
{
    std::lock_guard<std::mutex> lock(log_mutex);

    if (initialized)
        return LoggerError::ALREADY_INITIALIZED;

    config      = cfg;
    initialized = true;

    return LoggerError::SUCCESS;
}

const char* Logger::LevelToString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

LoggerError Logger::Log(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(log_mutex);

    if (!initialized)
        return LoggerError::NOT_INITIALIZED;

    if (static_cast<int>(level) < static_cast<int>(config.min_level))
        return LoggerError::SUCCESS;

    std::ostream& out =
        (config.output == LogOutput::STDERR) ? std::cerr : std::cout;

    if (config.show_timestamp)
        out << '[' << FormatTimestamp() << "] ";

    out << '[' << LevelToString(level) << ']';

    if (config.show_thread_id)
        out << "[tid=" << CurrentThreadId() << ']';

    out << ' ' << message << '\n';

    return LoggerError::SUCCESS;
}

LoggerError Logger::Debug(const std::string& message)
{
    return Log(LogLevel::DEBUG, message);
}

LoggerError Logger::Info(const std::string& message)
{
    return Log(LogLevel::INFO, message);
}

LoggerError Logger::Warn(const std::string& message)
{
    return Log(LogLevel::WARN, message);
}

LoggerError Logger::Error(const std::string& message)
{
    return Log(LogLevel::ERROR, message);
}

void Logger::SetMinLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(log_mutex);
    config.min_level = level;
}

LogLevel Logger::GetMinLevel() const
{
    std::lock_guard<std::mutex> lock(log_mutex);
    return config.min_level;
}

bool Logger::IsInitialized() const
{
    std::lock_guard<std::mutex> lock(log_mutex);
    return initialized;
}

void Logger::Shutdown()
{
    std::lock_guard<std::mutex> lock(log_mutex);
    initialized = false;
}