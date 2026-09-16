#include "framework/logger/logger.h"

#include <iostream>
#include <thread>
#include <vector>

bool run_logger_thread(
    Logger& logger,
    LogLevel level,
    const std::string& thread_name,
    int message_count)
{
    bool passed = true;

    for (int i = 0; i < message_count; ++i)
    {
        LoggerError result;

        switch (level)
        {
            case LogLevel::DEBUG:
                result = logger.Debug(
                    thread_name + " DEBUG message " + std::to_string(i));
                break;

            case LogLevel::INFO:
                result = logger.Info(
                    thread_name + " INFO message " + std::to_string(i));
                break;

            case LogLevel::WARN:
                result = logger.Warn(
                    thread_name + " WARN message " + std::to_string(i));
                break;

            case LogLevel::ERROR:
                result = logger.Error(
                    thread_name + " ERROR message " + std::to_string(i));
                break;
        }

        if (result != LoggerError::SUCCESS)
        {
            passed = false;
        }
    }

    return passed;
}

int main()
{
    Logger& logger = Logger::GetInstance();

    std::cout << "=== Logger Thread Safety Test ===\n\n";

    LoggerError result = logger.Initialize();

    if (result != LoggerError::SUCCESS)
    {
        std::cout << "Logger initialization FAILED\n";
        return 1;
    }

    constexpr int message_count = 100;

    bool debug_passed = false;
    bool info_passed = false;
    bool warn_passed = false;
    bool error_passed = false;

    std::thread debug_thread(
        [&logger, &debug_passed, message_count]()
        {
            debug_passed = run_logger_thread(
                logger,
                LogLevel::DEBUG,
                "Camera",
                message_count);
        });

    std::thread info_thread(
        [&logger, &info_passed, message_count]()
        {
            info_passed = run_logger_thread(
                logger,
                LogLevel::INFO,
                "Network",
                message_count);
        });

    std::thread warn_thread(
        [&logger, &warn_passed, message_count]()
        {
            warn_passed = run_logger_thread(
                logger,
                LogLevel::WARN,
                "MQTT",
                message_count);
        });

    std::thread error_thread(
        [&logger, &error_passed, message_count]()
        {
            error_passed = run_logger_thread(
                logger,
                LogLevel::ERROR,
                "RTSP",
                message_count);
        });

    debug_thread.join();
    info_thread.join();
    warn_thread.join();
    error_thread.join();

    logger.Shutdown();

    std::cout << "\n=== Test Results ===\n";

    std::cout << "DEBUG thread: "
              << (debug_passed ? "PASS" : "FAIL") << '\n';

    std::cout << "INFO thread:  "
              << (info_passed ? "PASS" : "FAIL") << '\n';

    std::cout << "WARN thread:  "
              << (warn_passed ? "PASS" : "FAIL") << '\n';

    std::cout << "ERROR thread: "
              << (error_passed ? "PASS" : "FAIL") << '\n';

    if (debug_passed &&
        info_passed &&
        warn_passed &&
        error_passed)
    {
        std::cout << "\nALL THREAD TESTS PASSED\n";
        return 0;
    }

    std::cout << "\nTHREAD TEST FAILURE\n";
    return 1;
}