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

        case LoggerError::NOT_INITIALIZED:
            return "NOT_INITIALIZED";
    }

    return "UNKNOWN";
}

bool check_result(
    const char* test_name,
    LoggerError actual,
    LoggerError expected)
{
    std::cout << "Test: " << test_name << '\n';
    std::cout << "Expected: " << error_to_string(expected) << '\n';
    std::cout << "Actual:   " << error_to_string(actual) << '\n';

    if (actual != expected)
    {
        std::cout << "Result:   FAIL\n\n";
        return false;
    }

    std::cout << "Result:   PASS\n\n";
    return true;
}

int main()
{
    Logger& logger = Logger::GetInstance();

    bool all_tests_passed = true;

    LoggerError result;

    result = logger.Info("Before initialization");

    all_tests_passed &= check_result(
        "Info before initialization",
        result,
        LoggerError::NOT_INITIALIZED);

    result = logger.Initialize();

    all_tests_passed &= check_result(
        "Logger initialization",
        result,
        LoggerError::SUCCESS);

    result = logger.Initialize();

    all_tests_passed &= check_result(
        "Double initialization",
        result,
        LoggerError::ALREADY_INITIALIZED);

    result = logger.Debug("Camera debug message");

    all_tests_passed &= check_result(
        "Debug logging",
        result,
        LoggerError::SUCCESS);

    result = logger.Info("Camera started");

    all_tests_passed &= check_result(
        "Info logging",
        result,
        LoggerError::SUCCESS);

    result = logger.Warn("Network connection retry");

    all_tests_passed &= check_result(
        "Warn logging",
        result,
        LoggerError::SUCCESS);

    result = logger.Error("Camera device unavailable");

    all_tests_passed &= check_result(
        "Error logging",
        result,
        LoggerError::SUCCESS);

    logger.Shutdown();

    result = logger.Info("After shutdown");

    all_tests_passed &= check_result(
        "Info after shutdown",
        result,
        LoggerError::NOT_INITIALIZED);

    if (all_tests_passed)
    {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }

    std::cout << "TEST FAILURE\n";
    return 1;
}