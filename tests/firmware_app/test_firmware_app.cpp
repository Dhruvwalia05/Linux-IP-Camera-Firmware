#include "app/firmware_app.h"

#include <chrono>
#include <cstdio>
#include <thread>

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
        std::fprintf(stderr, "[PASS] %s\n", name);
    }
    else
    {
        std::fprintf(stderr, "[FAIL] %s\n", name);
    }
}

} // namespace

int main()
{
        std::fprintf(stderr, "=== FirmwareApp Test ===\n\n");

    // Test 1: initial state
    {
        FirmwareApp app;
        Check(app.GetState() == FirmwareApp::State::UNINITIALIZED,
              "Default state is UNINITIALIZED");
    }

    // Test 2: Initialize succeeds
    {
        FirmwareApp app;
        Check(app.Initialize(), "Initialize succeeds");
        Check(app.GetState() == FirmwareApp::State::INITIALIZED,
              "State is INITIALIZED after Initialize");
        app.Shutdown();
    }

    // Test 3: second Initialize is rejected
    {
        FirmwareApp app;
        app.Initialize();
        Check(!app.Initialize(), "Second Initialize is rejected");
        Check(app.GetState() == FirmwareApp::State::INITIALIZED,
              "State unchanged after rejected second Initialize");
        app.Shutdown();
    }

    // Test 4: Shutdown before Initialize is a safe transition to
    // the terminal SHUTDOWN state. This ensures a subsequent
    // Initialize() is rejected.
    {
        FirmwareApp app;
        app.Shutdown();
        Check(app.GetState() == FirmwareApp::State::SHUTDOWN,
              "Shutdown before Initialize transitions to SHUTDOWN");
        Check(!app.Initialize(),
              "Initialize is rejected after early Shutdown");
    }

    // Test 5: Initialize then Shutdown transitions to SHUTDOWN
    {
        FirmwareApp app;
        app.Initialize();
        app.Shutdown();
        Check(app.GetState() == FirmwareApp::State::SHUTDOWN,
              "State is SHUTDOWN after Shutdown");
    }

    // Test 6: double Shutdown is safe
    {
        FirmwareApp app;
        app.Initialize();
        app.Shutdown();
        app.Shutdown();
        Check(app.GetState() == FirmwareApp::State::SHUTDOWN,
              "Double Shutdown is safe and preserves SHUTDOWN state");
    }

    // Test 7: Initialize after Shutdown is rejected
    {
        FirmwareApp app;
        app.Initialize();
        app.Shutdown();
        Check(!app.Initialize(),
              "Initialize after Shutdown is rejected");
    }

    // Test 8: RequestShutdown returns Run() cleanly
    {
        FirmwareApp app;
        app.Initialize();

        std::thread runner([&app]() { app.Run(); });

        // Give Run() time to enter the loop.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        Check(app.GetState() == FirmwareApp::State::RUNNING,
              "State is RUNNING while Run() executes");

        app.RequestShutdown();
        runner.join();

        Check(app.GetState() == FirmwareApp::State::RUNNING,
              "State remains RUNNING after Run() returns");

        app.Shutdown();
        Check(app.GetState() == FirmwareApp::State::SHUTDOWN,
              "State is SHUTDOWN after explicit Shutdown");
    }

    // Test 9: RequestShutdown before Run() prevents loop blocking
    {
        FirmwareApp app;
        app.Initialize();

        app.RequestShutdown();

        const auto start = std::chrono::steady_clock::now();
        app.Run();
        const auto elapsed = std::chrono::steady_clock::now() - start;

        Check(elapsed < std::chrono::milliseconds(500),
              "Run() returns promptly when shutdown pre-requested");

        app.Shutdown();
    }

    // Test 10: Run() without Initialize is a no-op
    {
        FirmwareApp app;

        const auto start = std::chrono::steady_clock::now();
        app.Run();
        const auto elapsed = std::chrono::steady_clock::now() - start;

        Check(elapsed < std::chrono::milliseconds(50),
              "Run() without Initialize returns immediately");
        Check(app.GetState() == FirmwareApp::State::UNINITIALIZED,
              "State unchanged by Run() without Initialize");
    }

    // Test 11: destructor safety net cleans up after partial lifecycle
    {
        {
            FirmwareApp app;
            app.Initialize();
            // Intentional: no explicit Shutdown().
        }
        // If we get here without crashing, the safety net worked.

        // Verify subsequent FirmwareApp can initialize —
        // proves SignalHandler was properly restored.
        FirmwareApp app;
        Check(app.Initialize(),
              "Next FirmwareApp Initializes after destructor cleanup");
        app.Shutdown();
    }

    // Test 12: RequestShutdown before Initialize is safe
    {
        FirmwareApp app;
        app.RequestShutdown();
        Check(app.Initialize(),
              "Initialize succeeds after pre-Initialize RequestShutdown");
        app.Shutdown();
    }

    std::fprintf(stderr, "\n=== Summary ===\n");
    std::fprintf(stderr, "Tests run:    %d\n", tests_run);
    std::fprintf(stderr, "Tests passed: %d\n", tests_passed);

    if (tests_run == tests_passed)
    {
        std::fprintf(stderr, "ALL FIRMWAREAPP TESTS PASSED\n");
        return 0;
    }

    std::fprintf(stderr, "FIRMWAREAPP TESTS FAILED\n");
    return 1;
    return 1;
}