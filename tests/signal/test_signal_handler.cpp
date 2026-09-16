#include "framework/signal/signal_handler.h"

#include <csignal>
#include <iostream>

int main()
{
    SignalHandler signalHandler;

    // Test 1: Initialize
    if (!signalHandler.Initialize())
    {
        std::cerr << "TEST FAILED: SignalHandler initialization\n";
        return 1;
    }

    std::cout << "TEST PASSED: SignalHandler initialization\n";

    // Test 2: Initial shutdown state
    if (signalHandler.IsShutdownRequested())
    {
        std::cerr << "TEST FAILED: Shutdown requested initially\n";
        return 1;
    }

    std::cout << "TEST PASSED: Initial shutdown state\n";

    // Test 3: SIGINT
    raise(SIGINT);

    if (!signalHandler.IsShutdownRequested())
    {
        std::cerr << "TEST FAILED: SIGINT did not request shutdown\n";
        return 1;
    }

    std::cout << "TEST PASSED: SIGINT handling\n";

    // Test 4: Shutdown resets state
    signalHandler.Shutdown();

    if (signalHandler.IsShutdownRequested())
    {
        std::cerr << "TEST FAILED: Shutdown did not reset state\n";
        return 1;
    }

    std::cout << "TEST PASSED: Shutdown reset\n";

    // Test 5: SIGTERM
    raise(SIGTERM);

    if (!signalHandler.IsShutdownRequested())
    {
        std::cerr << "TEST FAILED: SIGTERM did not request shutdown\n";
        return 1;
    }

    std::cout << "TEST PASSED: SIGTERM handling\n";

    std::cout << "ALL SIGNAL HANDLER TESTS PASSED\n";

    return 0;
}