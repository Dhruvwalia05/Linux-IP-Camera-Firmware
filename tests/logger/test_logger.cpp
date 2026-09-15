#include "framework/logger/logger.h"
#include <iostream>

const char* error_to_string(LoggerError error)
{
    switch (error)
    {
        case LoggerError::SUCCESS:
            return "SUCCESS";

        case LoggerError::ALREADY_INITIALIZED:
            return "ALREADY_INITIALIZED";

        case LoggerError::FILE_OPEN_FAILED:
            return "FILE_OPEN_FAILED";

        case LoggerError::NOT_INITIALIZED:
            return "NOT_INITIALIZED";

        case LoggerError::WRITE_FAILED:
            return "WRITE_FAILED";
    }

    return "UNKNOWN";
}

int main(){
    Logger& logger = Logger::GetInstance();

    LoggerError result;

    result = logger.Info("Before initialization");
    std::cout << error_to_string(result) << '\n';

    result = logger.Initialize("/tmp/firmware.log");
    std::cout << error_to_string(result) << '\n';

    result = logger.Info("Camera Started");
    std::cout << error_to_string(result) << '\n';

    result = logger.Initialize("/tmp/firmware.log");
    std::cout << error_to_string(result) << '\n';
}