#ifndef LOGGER_H
#define LOGGER_H

#include <mutex>
#include <string>

enum class LoggerError
{
    SUCCESS,
    ALREADY_INITIALIZED,
    NOT_INITIALIZED,
    INVALID_ARGUMENT
};

enum class LogLevel
{
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

enum class LogOutput
{
    STDOUT,
    STDERR
};

struct LoggerConfig
{
    LogLevel  min_level      = LogLevel::DEBUG;
    LogOutput output         = LogOutput::STDOUT;
    bool      show_timestamp = true;
    bool      show_thread_id = true;
};

class Logger
{
public:
    static Logger& GetInstance();

    LoggerError Initialize();
    LoggerError Initialize(const LoggerConfig& config);

    LoggerError Debug(const std::string& message);
    LoggerError Info (const std::string& message);
    LoggerError Warn (const std::string& message);
    LoggerError Error(const std::string& message);

    void     SetMinLevel(LogLevel level);
    LogLevel GetMinLevel() const;
    bool     IsInitialized() const;

    void Shutdown();

private:
    Logger();
    ~Logger();

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&)                 = delete;
    Logger& operator=(Logger&&)      = delete;

    LoggerError Log(LogLevel level, const std::string& message);
    static const char* LevelToString(LogLevel level);

    bool         initialized;
    LoggerConfig config;
    mutable std::mutex   log_mutex;
};

#endif // LOGGER_H