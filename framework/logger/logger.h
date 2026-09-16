#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>

enum class LoggerError
{
    SUCCESS,
    ALREADY_INITIALIZED,
    NOT_INITIALIZED
};

enum class LogLevel
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger
{
    Logger();

    bool initialized;
    std::mutex log_mutex;

    LoggerError Log(LogLevel level, const std::string& message);

public:
    ~Logger();

    static Logger& GetInstance();

    LoggerError Initialize();

    LoggerError Debug(const std::string& message);
    LoggerError Info(const std::string& message);
    LoggerError Warn(const std::string& message);
    LoggerError Error(const std::string& message);

    void Shutdown();
};

#endif