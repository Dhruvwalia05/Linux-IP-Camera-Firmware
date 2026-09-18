#include "framework/signal/signal_handler.h"

#include <csignal>
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
    std::cout << "=== SignalHandler Test ===\n\n";

    // Test 1: initial state
    {
        SignalHandler handler;

        Check(!handler.IsInstalled(),         "Not installed at construction");
        Check(!handler.IsShutdownRequested(), "Shutdown not requested at start");
    }

    // Test 2: Initialize succeeds
    {
        SignalHandler handler;

        Check(handler.Initialize(),           "Initialize succeeds");
        Check(handler.IsInstalled(),          "IsInstalled true after Initialize");
        Check(!handler.IsShutdownRequested(), "Flag clear after Initialize");

        handler.Shutdown();
    }

    // Test 3: double Initialize is rejected
    {
        SignalHandler handler;
        handler.Initialize();

        Check(!handler.Initialize(),          "Double Initialize is rejected");

        handler.Shutdown();
    }

    // Test 4: SIGINT sets flag
    {
        SignalHandler handler;
        handler.Initialize();

        raise(SIGINT);

        Check(handler.IsShutdownRequested(),  "SIGINT sets shutdown flag");

        handler.Shutdown();
    }

    // Test 5: SIGTERM sets flag
    {
        SignalHandler handler;
        handler.Initialize();

        raise(SIGTERM);

        Check(handler.IsShutdownRequested(),  "SIGTERM sets shutdown flag");

        handler.Shutdown();
    }

    // Test 6: Shutdown resets state and flag
    {
        SignalHandler handler;
        handler.Initialize();

        raise(SIGINT);

        Check(handler.IsShutdownRequested(),  "Flag set before Shutdown");

        handler.Shutdown();

        Check(!handler.IsInstalled(),         "Not installed after Shutdown");
        Check(!handler.IsShutdownRequested(), "Flag cleared after Shutdown");
    }

    // Test 7: Shutdown without Initialize is safe no-op
    {
        SignalHandler handler;

        handler.Shutdown();

        Check(!handler.IsInstalled(),         "Shutdown without Initialize is safe");
    }

    // Test 8: re-initialize after Shutdown works
    {
        SignalHandler handler;

        handler.Initialize();
        raise(SIGINT);
        handler.Shutdown();

        Check(handler.Initialize(),           "Re-Initialize after Shutdown succeeds");
        Check(handler.IsInstalled(),          "Installed after re-Initialize");
        Check(!handler.IsShutdownRequested(), "Flag clear after re-Initialize");

        handler.Shutdown();
    }

    // Test 9: destructor cleans up without explicit Shutdown
    {
        {
            SignalHandler handler;
            handler.Initialize();
            raise(SIGINT);
            // Intentional: no explicit Shutdown.
        }

        SignalHandler handler;

        Check(handler.Initialize(),           "Initialize succeeds after destructor cleanup");
        Check(!handler.IsShutdownRequested(), "Flag clear after destructor cleanup");

        handler.Shutdown();
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "Tests run:    " << tests_run    << '\n';
    std::cout << "Tests passed: " << tests_passed << '\n';

    if (tests_run == tests_passed)
    {
        std::cout << "ALL SIGNAL HANDLER TESTS PASSED\n";
        return 0;
    }

    std::cout << "SIGNAL HANDLER TESTS FAILED\n";
    return 1;
}