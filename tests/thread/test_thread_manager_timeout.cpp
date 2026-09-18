#include "framework/thread/thread_manager.h"
#include "framework/util/helgrind_annotations.h"

#include <pthread.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
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
    std::cout << "=== ThreadManager Timeout / Naming Test ===\n\n";

    // Test 1: Join(timeout) succeeds when worker finishes in time
    {
        ThreadManager manager;

        manager.Start([](StopToken& token)
        {
            while (!token.IsStopRequested())
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
        });

        manager.Stop();

        auto result = manager.Join(std::chrono::milliseconds(2000));

        Check(result == ThreadError::SUCCESS,
              "Join(timeout) succeeds when worker finishes");
        Check(!manager.IsRunning(),
              "Not running after Join(timeout) success");
    }

    // Test 2: Join(timeout) returns JOIN_TIMEOUT when worker ignores stop
    {
        ThreadManager manager;

        std::atomic<bool> allow_exit(false);

        manager.Start([&allow_exit](StopToken&)
        {
            while (true)
            {
                if (allow_exit.load())
                {
                    ANNOTATE_HAPPENS_AFTER(&allow_exit);
                    break;
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
            }
        });

        auto result = manager.Join(std::chrono::milliseconds(100));

        Check(result == ThreadError::JOIN_TIMEOUT,
              "Join(timeout) returns JOIN_TIMEOUT");
        Check(manager.IsRunning(),
              "Still running after JOIN_TIMEOUT");

        allow_exit.store(true);
        ANNOTATE_HAPPENS_BEFORE(&allow_exit);

        result = manager.Join(std::chrono::milliseconds(2000));

        Check(result == ThreadError::SUCCESS,
              "Join(timeout) succeeds after retry");
        Check(!manager.IsRunning(),
              "Not running after successful retry");
    }

    // Test 3: Join(timeout) before Start
    {
        ThreadManager manager;

        auto result = manager.Join(std::chrono::milliseconds(100));

        Check(result == ThreadError::NOT_RUNNING,
              "Join(timeout) before Start is NOT_RUNNING");
    }

    // Test 4: blocking Join() still works after a JOIN_TIMEOUT
    {
        ThreadManager manager;

        std::atomic<bool> allow_exit(false);

        manager.Start([&allow_exit](StopToken&)
        {
            while (true)
            {
                if (allow_exit.load())
                {
                    ANNOTATE_HAPPENS_AFTER(&allow_exit);
                    break;
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
            }
        });

        auto result = manager.Join(std::chrono::milliseconds(50));
        Check(result == ThreadError::JOIN_TIMEOUT,
              "First Join times out");

        allow_exit.store(true);
        ANNOTATE_HAPPENS_BEFORE(&allow_exit);

        result = manager.Join();
        Check(result == ThreadError::SUCCESS,
              "Blocking Join after timeout succeeds");
    }

    // Test 5: Start(name, worker) succeeds
    {
        ThreadManager manager;

        auto result = manager.Start("camera",
            [](StopToken& token)
            {
                while (!token.IsStopRequested())
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
            });

        Check(result == ThreadError::SUCCESS,
              "Start(name, worker) succeeds");

        manager.Stop();
        manager.Join();
    }

    // Test 6: empty name uses default
    {
        ThreadManager manager;

        auto result = manager.Start("",
            [](StopToken& token)
            {
                while (!token.IsStopRequested())
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
            });

        Check(result == ThreadError::SUCCESS,
              "Start with empty name succeeds");

        manager.Stop();
        manager.Join();
    }

    // Test 7: thread name is actually applied
    {
        ThreadManager manager;

        std::atomic<bool> name_verified(false);
        std::string observed_name;

        manager.Start("cam0",
            [&](StopToken& token)
            {
                char buf[64] = {0};
                pthread_getname_np(pthread_self(), buf, sizeof(buf));
                observed_name = buf;

                ANNOTATE_HAPPENS_BEFORE(&name_verified);
                name_verified.store(true);

                while (!token.IsStopRequested())
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
            });

        for (int i = 0; i < 1000 && !name_verified.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        ANNOTATE_HAPPENS_AFTER(&name_verified);

        Check(name_verified.load(),
              "Worker reported its own thread name");
        Check(observed_name == "cam0",
              "Worker thread name matches requested name");

        manager.Stop();
        manager.Join();
    }

    // Test 8: name longer than 15 chars is truncated
    {
        ThreadManager manager;

        std::atomic<bool> name_verified(false);
        std::string observed_name;

        manager.Start("camera_streaming_worker",
            [&](StopToken& token)
            {
                char buf[64] = {0};
                pthread_getname_np(pthread_self(), buf, sizeof(buf));
                observed_name = buf;

                ANNOTATE_HAPPENS_BEFORE(&name_verified);
                name_verified.store(true);

                while (!token.IsStopRequested())
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
            });

        for (int i = 0; i < 1000 && !name_verified.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));

        ANNOTATE_HAPPENS_AFTER(&name_verified);

        Check(name_verified.load(),
              "Long-name worker started");
        Check(observed_name == "camera_streamin",
              "Long name truncated to 15 characters");

        manager.Stop();
        manager.Join();
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "Tests run:    " << tests_run << '\n';
    std::cout << "Tests passed: " << tests_passed << '\n';

    if (tests_run == tests_passed)
    {
        std::cout << "ALL THREADMANAGER TIMEOUT TESTS PASSED\n";
        return 0;
    }

    std::cout << "THREADMANAGER TIMEOUT TESTS FAILED\n";
    return 1;
}