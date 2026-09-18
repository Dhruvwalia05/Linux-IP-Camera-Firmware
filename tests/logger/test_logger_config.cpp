#include "framework/logger/logger.h"

#include <iostream>

namespace
{

int tests_run    = 0;
int tests_passed = 0;

void Check(bool condition, const char* name)
{
    ++tests_run;
    if (condition)
    {
        ++tests_passed;
        std::cout << "[PASS] " << name << '\n';
    }
    else
    {
        std::cout << "[FAIL] " << name << '\n';
    }
}

} // namespace

int main()
{
    Logger& logger = Logger::GetInstance();

    std::cout << "=== Logger Config Test ===\n\n";

    Check(logger.IsInitialized() == false, "Not initialized at start");

    LoggerConfig config;
    config.min_level      = LogLevel::DEBUG;
    config.output         = LogOutput::STDOUT;
    config.show_timestamp = true;
    config.show_thread_id = true;

    LoggerError result = logger.Initialize(config);
    Check(result == LoggerError::SUCCESS, "Initialize with config");
    Check(logger.IsInitialized(), "IsInitialized true after Initialize");

    std::cout << "\n-- All levels should appear below --\n";
    logger.Debug("debug visible");
    logger.Info ("info visible");
    logger.Warn ("warn visible");
    logger.Error("error visible");

    logger.SetMinLevel(LogLevel::WARN);
    Check(logger.GetMinLevel() == LogLevel::WARN, "SetMinLevel/GetMinLevel");

    std::cout << "\n-- Only WARN and ERROR should appear below --\n";
    logger.Debug("debug SHOULD NOT appear");
    logger.Info ("info SHOULD NOT appear");
    logger.Warn ("warn visible");
    logger.Error("error visible");

    logger.SetMinLevel(LogLevel::ERROR);
    Check(logger.GetMinLevel() == LogLevel::ERROR, "SetMinLevel to ERROR");

    std::cout << "\n-- Only ERROR should appear below --\n";
    logger.Debug("SHOULD NOT appear");
    logger.Info ("SHOULD NOT appear");
    logger.Warn ("SHOULD NOT appear");
    logger.Error("error visible");

    logger.Shutdown();
    Check(logger.IsInitialized() == false, "IsInitialized false after Shutdown");

    std::cout << "\n=== Summary ===\n";
    std::cout << "Tests run:    " << tests_run    << '\n';
    std::cout << "Tests passed: " << tests_passed << '\n';

    if (tests_run == tests_passed)
    {
        std::cout << "ALL LOGGER CONFIG TESTS PASSED\n";
        return 0;
    }

    std::cout << "LOGGER CONFIG TESTS FAILED\n";
    return 1;
}