#ifndef LOGGER_H
#define LOGGER_H
#include <string>

enum class LoggerError
{
    SUCCESS,
    ALREADY_INITIALIZED,
    FILE_OPEN_FAILED,
    NOT_INITIALIZED,
    WRITE_FAILED
};

class Logger
{
    Logger();

    int file_descriptor = -1;
    bool initialized = false;
    
public:

    ~Logger();

    static Logger& GetInstance();

    LoggerError Initialize(const std::string& log_file);

    LoggerError Debug(const std::string& message);
    LoggerError Info(const std::string& message);
    LoggerError Warn(const std::string& message);
    LoggerError Error(const std::string& message);

    void Shutdown();
};

#endif