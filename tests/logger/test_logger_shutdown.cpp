#include "framework/logger/logger.h"

#include <array>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

bool check_result(
    const char* test_name,
    LoggerError actual,
    LoggerError expected)
{
    std::cout << "Test: " << test_name << '\n';
    std::cout << "Expected: " << static_cast<int>(expected) << '\n';
    std::cout << "Actual:   " << static_cast<int>(actual) << '\n';

    if (actual != expected)
    {
        std::cout << "Result:   FAIL\n\n";
        return false;
    }

    std::cout << "Result:   PASS\n\n";
    return true;
}

bool run_logger_thread(
    Logger& logger,
    LogLevel level,
    const std::string& thread_name,
    int message_count)
{
    bool success = true;

    for (int i = 0; i < message_count; ++i)
    {
        LoggerError result;

        std::string message =
            thread_name +
            " message " +
            std::to_string(i);

        switch (level)
        {
            case LogLevel::DEBUG:
                result = logger.Debug(message);
                break;

            case LogLevel::INFO:
                result = logger.Info(message);
                break;

            case LogLevel::WARN:
                result = logger.Warn(message);
                break;

            case LogLevel::ERROR:
                result = logger.Error(message);
                break;
        }

        if (result != LoggerError::SUCCESS)
        {
            success = false;
        }
    }

    return success;
}

int main()
{
    Logger& logger = Logger::GetInstance();

    bool all_tests_passed = true;

    /*
     * Test 1:
     * Logger should initialize successfully.
     */
    LoggerError result = logger.Initialize();

    all_tests_passed &= check_result(
        "Logger initialization",
        result,
        LoggerError::SUCCESS);

    /*
     * Test 2:
     * Logging should work before shutdown.
     */
    result = logger.Info("Logging before shutdown");

    all_tests_passed &= check_result(
        "Logging before shutdown",
        result,
        LoggerError::SUCCESS);

    /*
     * Test 3:
     * Multiple threads use all four log levels concurrently.
     */
    constexpr int thread_count = 4;
    constexpr int messages_per_thread = 100;

    std::array<std::atomic<bool>, thread_count> thread_results;

    for (auto& thread_result : thread_results)
    {
        thread_result.store(false);
    }

    std::vector<std::thread> threads;

    threads.emplace_back(
        [&logger, &thread_results]()
        {
            thread_results[0].store(
                run_logger_thread(
                    logger,
                    LogLevel::DEBUG,
                    "Camera",
                    messages_per_thread));
        });

    threads.emplace_back(
        [&logger, &thread_results]()
        {
            thread_results[1].store(
                run_logger_thread(
                    logger,
                    LogLevel::INFO,
                    "Network",
                    messages_per_thread));
        });

    threads.emplace_back(
        [&logger, &thread_results]()
        {
            thread_results[2].store(
                run_logger_thread(
                    logger,
                    LogLevel::WARN,
                    "MQTT",
                    messages_per_thread));
        });

    threads.emplace_back(
        [&logger, &thread_results]()
        {
            thread_results[3].store(
                run_logger_thread(
                    logger,
                    LogLevel::ERROR,
                    "RTSP",
                    messages_per_thread));
        });

    /*
     * Wait for all logging threads to finish.
     */
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    bool concurrent_logging_passed = true;

    for (const auto& thread_result : thread_results)
    {
        if (!thread_result.load())
        {
            concurrent_logging_passed = false;
            break;
        }
    }

    std::cout << "Test: Concurrent logging with all log levels\n";

    if (concurrent_logging_passed)
    {
        std::cout << "Result: PASS\n\n";
    }
    else
    {
        std::cout << "Result: FAIL\n\n";
        all_tests_passed = false;
    }

    /*
     * Test 4:
     * Shutdown should complete.
     */
    logger.Shutdown();

    std::cout << "Test: Logger shutdown\n";
    std::cout << "Result: PASS\n\n";

    /*
     * Test 5:
     * Info logging after shutdown must fail.
     */
    result = logger.Info("Info after shutdown");

    all_tests_passed &= check_result(
        "Info after shutdown",
        result,
        LoggerError::NOT_INITIALIZED);

    /*
     * Test 6:
     * All log levels must reject logging after shutdown.
     */
    result = logger.Debug("Debug after shutdown");

    all_tests_passed &= check_result(
        "Debug after shutdown",
        result,
        LoggerError::NOT_INITIALIZED);

    result = logger.Warn("Warn after shutdown");

    all_tests_passed &= check_result(
        "Warn after shutdown",
        result,
        LoggerError::NOT_INITIALIZED);

    result = logger.Error("Error after shutdown");

    all_tests_passed &= check_result(
        "Error after shutdown",
        result,
        LoggerError::NOT_INITIALIZED);

    /*
     * Test 7:
     * Logger can be initialized again after shutdown.
     */
    result = logger.Initialize();

    all_tests_passed &= check_result(
        "Re-initialization after shutdown",
        result,
        LoggerError::SUCCESS);

    logger.Shutdown();

    /*
     * Final result.
     */
    if (all_tests_passed)
    {
        std::cout << "ALL SHUTDOWN TESTS PASSED\n";
        return 0;
    }

    std::cout << "SHUTDOWN TEST FAILURE\n";
    return 1;
}