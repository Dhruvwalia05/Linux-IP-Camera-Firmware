#include "framework/thread/thread_manager.h"
#include "framework/util/helgrind_annotations.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace
{

#ifndef STRESS_ITERATIONS
#define STRESS_ITERATIONS 1000
#endif

#ifndef CONCURRENT_THREADS
#define CONCURRENT_THREADS 8
#endif

constexpr int kStressIterations  = STRESS_ITERATIONS;
constexpr int kConcurrentThreads = CONCURRENT_THREADS;

void PrintResult(const char* name, bool passed)
{
    std::cout << (passed ? "STRESS PASSED: " : "STRESS FAILED: ")
              << name << std::endl;
}

/*
 * Repeated Start -> Stop -> Join.
 */
bool TestRepeatedLifecycle()
{
    ThreadManager manager;

    for (int i = 0; i < STRESS_ITERATIONS; ++i)
    {
        std::atomic<bool> worker_started(false);

        auto worker = [&worker_started](StopToken& token)
        {
            worker_started.store(true);
            ANNOTATE_HAPPENS_BEFORE(&worker_started);

            while (!token.IsStopRequested())
            {
                std::this_thread::yield();
            }
        };

        if (manager.Start(worker) != ThreadError::SUCCESS)
        {
            return false;
        }

        /*
         * Give the worker a bounded amount of time to start.
         */
        for (int retry = 0;
             retry < 1000 && !worker_started.load();
             ++retry)
        {
            std::this_thread::sleep_for(
                std::chrono::microseconds(100));
        }

        ANNOTATE_HAPPENS_AFTER(&worker_started);

        if (!worker_started.load())
        {
            manager.Stop();
            manager.Join();

            return false;
        }

        if (manager.Stop() != ThreadError::SUCCESS)
        {
            manager.Join();
            return false;
        }

        if (manager.Join() != ThreadError::SUCCESS)
        {
            return false;
        }

        if (manager.IsRunning())
        {
            return false;
        }
    }

    return true;
}

/*
 * Repeated concurrent Stop calls.
 *
 * Stop() is intentionally idempotent while the manager
 * remains in the Running state.
 */
bool TestConcurrentStop()
{
    for (int iteration = 0;
         iteration < STRESS_ITERATIONS;
         ++iteration)
    {
        ThreadManager manager;

        std::atomic<bool> worker_started(false);

        auto worker = [&worker_started](StopToken& token)
        {
            worker_started.store(true);
            ANNOTATE_HAPPENS_BEFORE(&worker_started);

            while (!token.IsStopRequested())
            {
                std::this_thread::yield();
            }
        };

        if (manager.Start(worker) != ThreadError::SUCCESS)
        {
            return false;
        }

        for (int retry = 0;
             retry < 1000 && !worker_started.load();
             ++retry)
        {
            std::this_thread::sleep_for(
                std::chrono::microseconds(100));
        }

        ANNOTATE_HAPPENS_AFTER(&worker_started);

        if (!worker_started.load())
        {
            manager.Stop();
            manager.Join();

            return false;
        }

        std::vector<std::thread> threads;

        std::atomic<int> success_count(0);
        ANNOTATE_ATOMIC_COUNTER(&success_count);

        for (int i = 0; i < CONCURRENT_THREADS; ++i)
        {
            threads.emplace_back(
                [&manager, &success_count]()
                {
                    if (manager.Stop() == ThreadError::SUCCESS)
                    {
                        success_count.fetch_add(1);
                    }
                });
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        /*
         * Since running remains true until Join(), every
         * concurrent Stop() is allowed to return SUCCESS.
         */
        if (success_count.load() != CONCURRENT_THREADS)
        {
            manager.Stop();
            manager.Join();

            return false;
        }

        if (manager.Join() != ThreadError::SUCCESS)
        {
            return false;
        }

        if (manager.IsRunning())
        {
            return false;
        }
    }

    return true;
}

/*
 * Repeated concurrent IsRunning calls while the worker
 * remains active.
 */
bool TestConcurrentIsRunning()
{
    ThreadManager manager;

    std::atomic<bool> worker_started(false);

    auto worker = [&worker_started](StopToken& token)
    {
        worker_started.store(true);
        ANNOTATE_HAPPENS_BEFORE(&worker_started);

        while (!token.IsStopRequested())
        {
            std::this_thread::yield();
        }
    };

    if (manager.Start(worker) != ThreadError::SUCCESS)
    {
        return false;
    }

    for (int retry = 0;
         retry < 1000 && !worker_started.load();
         ++retry)
    {
        std::this_thread::sleep_for(
            std::chrono::microseconds(100));
    }

    ANNOTATE_HAPPENS_AFTER(&worker_started);

    if (!worker_started.load())
    {
        manager.Stop();
        manager.Join();

        return false;
    }

    std::atomic<bool> test_failed(false);
    ANNOTATE_ATOMIC_COUNTER(&test_failed);

    std::vector<std::thread> threads;

    for (int i = 0; i < CONCURRENT_THREADS; ++i)
    {
        threads.emplace_back(
            [&manager, &test_failed]()
            {
                for (int j = 0; j < 10000; ++j)
                {
                    if (!manager.IsRunning())
                    {
                        test_failed.store(true);
                        return;
                    }
                }
            });
    }

    for (auto& thread : threads)
    {
        thread.join();
    }

    if (manager.Stop() != ThreadError::SUCCESS)
    {
        manager.Join();
        return false;
    }

    if (manager.Join() != ThreadError::SUCCESS)
    {
        return false;
    }

    return !test_failed.load();
}

/*
 * Repeated concurrent Start attempts.
 */
bool TestConcurrentStart()
{
    for (int iteration = 0;
         iteration < STRESS_ITERATIONS;
         ++iteration)
    {
        ThreadManager manager;

        std::atomic<int> start_success_count(0);
        ANNOTATE_ATOMIC_COUNTER(&start_success_count);

        auto worker = [](StopToken& token)
        {
            while (!token.IsStopRequested())
            {
                std::this_thread::yield();
            }
        };

        std::vector<std::thread> threads;

        for (int i = 0; i < CONCURRENT_THREADS; ++i)
        {
            threads.emplace_back(
                [&manager, &worker, &start_success_count]()
                {
                    if (manager.Start(worker) == ThreadError::SUCCESS)
                    {
                        start_success_count.fetch_add(1);
                    }
                });
        }

        for (auto& thread : threads)
        {
            thread.join();
        }

        /*
         * Exactly one concurrent Start() should succeed.
         */
        if (start_success_count.load() != 1)
        {
            /*
             * Defensive cleanup in case one thread did start.
             */
            manager.Stop();
            manager.Join();

            return false;
        }

        if (manager.Stop() != ThreadError::SUCCESS)
        {
            manager.Join();
            return false;
        }

        if (manager.Join() != ThreadError::SUCCESS)
        {
            return false;
        }

        if (manager.IsRunning())
        {
            return false;
        }
    }

    return true;
}

} // namespace

int main()
{
    bool all_tests_passed = true;

    bool repeated_lifecycle = TestRepeatedLifecycle();

    PrintResult(
        "Repeated lifecycle",
        repeated_lifecycle);

    all_tests_passed =
        repeated_lifecycle && all_tests_passed;

    bool concurrent_stop = TestConcurrentStop();

    PrintResult(
        "Concurrent Stop stress",
        concurrent_stop);

    all_tests_passed =
        concurrent_stop && all_tests_passed;

    bool concurrent_is_running = TestConcurrentIsRunning();

    PrintResult(
        "Concurrent IsRunning stress",
        concurrent_is_running);

    all_tests_passed =
        concurrent_is_running && all_tests_passed;

    bool concurrent_start = TestConcurrentStart();

    PrintResult(
        "Concurrent Start stress",
        concurrent_start);

    all_tests_passed =
        concurrent_start && all_tests_passed;

    if (all_tests_passed)
    {
        std::cout
            << "ALL THREAD MANAGER STRESS TESTS PASSED"
            << std::endl;

        return 0;
    }

    std::cout
        << "THREAD MANAGER STRESS TESTS FAILED"
        << std::endl;

    return 1;
}