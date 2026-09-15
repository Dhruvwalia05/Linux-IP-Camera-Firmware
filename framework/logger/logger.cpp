#include "logger.h"
#include <fcntl.h>
#include <unistd.h>

Logger& Logger::GetInstance(){

    static Logger instance;

    return instance;
}

Logger::Logger()
    :   file_descriptor(-1),
        initialized(false)
{

}

Logger::~Logger(){

}

LoggerError Logger::Initialize(const std::string& log_file)
{
    if (file_descriptor != -1 || initialized)
        return LoggerError::ALREADY_INITIALIZED;

    file_descriptor = open(log_file.c_str(), O_WRONLY |O_APPEND | O_CREAT, 0600);

    if(file_descriptor == -1)
        return LoggerError::FILE_OPEN_FAILED;

    initialized = true;

    return LoggerError::SUCCESS;
    
}

LoggerError Logger::Info(const std::string& message){

    if (!initialized)
        return LoggerError::NOT_INITIALIZED;

    std::string log_message = message + "\n";

    ssize_t bytes_written = write(
        file_descriptor, 
        log_message.c_str(), 
        log_message.size());

    if(bytes_written == -1 || 
        bytes_written != static_cast<ssize_t>(log_message.size()))
        return LoggerError::WRITE_FAILED;

    return LoggerError::SUCCESS;
}