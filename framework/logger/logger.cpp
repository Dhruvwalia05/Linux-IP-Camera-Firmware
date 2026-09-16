#include "logger.h"

#include <iostream>
#include <mutex>

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
    std::lock_guard<std::mutex> lock(log_mutex);

    if (initialized)
        return LoggerError::ALREADY_INITIALIZED;

    initialized = true;

    return LoggerError::SUCCESS;
}

LoggerError Logger::Log(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(log_mutex);

    if (!initialized)
        return LoggerError::NOT_INITIALIZED;

    const char* level_string = nullptr;

    switch (level)
    {
        case LogLevel::DEBUG:
            level_string = "DEBUG";
            break;

        case LogLevel::INFO:
            level_string = "INFO";
            break;

        case LogLevel::WARN:
            level_string = "WARN";
            break;

        case LogLevel::ERROR:
            level_string = "ERROR";
            break;
    }

    std::cout << "[" << level_string << "] "
              << message << '\n';

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

void Logger::Shutdown()
{
    std::lock_guard<std::mutex> lock(log_mutex);
    initialized = false;
}