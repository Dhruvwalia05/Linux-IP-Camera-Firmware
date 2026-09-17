#include "framework/thread/thread_manager.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

namespace
{

void PrintResult(const char* test_name, bool passed)
{
    if (passed)
    {
        std::cout << "TEST PASSED: " << test_name << '\n';
    }
    else
    {
        std::cerr << "TEST FAILED: " << test_name << '\n';
    }
}

bool TestInitialState()
{
    ThreadManager manager;

    bool passed = !manager.IsRunning();

    PrintResult("Initial state", passed);

    return passed;
}

bool TestInvalidWorker()
{
    ThreadManager manager;

    ThreadError result =
        manager.Start(std::function<void(StopToken&)>{});

    bool passed = result == ThreadError::INVALID_ARGUMENT;

    PrintResult("Invalid worker", passed);

    return passed;
}

bool TestStart()
{
    ThreadManager manager;

    ThreadError result = manager.Start(
        [](StopToken& token)
        {
            while (!token.IsStopRequested())
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }
        });

    bool passed = result == ThreadError::SUCCESS &&
                  manager.IsRunning();

    PrintResult("Start", passed);

    manager.Stop();
    manager.Join();

    return passed;
}

bool TestDoubleStart()
{
    ThreadManager manager;

    auto worker = [](StopToken& token)
    {
        while (!token.IsStopRequested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    };

    manager.Start(worker);

    ThreadError result = manager.Start(worker);

    bool passed = result == ThreadError::ALREADY_RUNNING;

    PrintResult("Double Start", passed);

    manager.Stop();
    manager.Join();

    return passed;
}

bool TestStop()
{
    ThreadManager manager;

    manager.Start(
        [](StopToken& token)
        {
            while (!token.IsStopRequested())
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }
        });

    ThreadError result = manager.Stop();

    bool passed = result == ThreadError::SUCCESS;

    PrintResult("Stop", passed);

    manager.Join();

    return passed;
}

bool TestStopBeforeStart()
{
    ThreadManager manager;

    ThreadError result = manager.Stop();

    bool passed = result == ThreadError::NOT_RUNNING;

    PrintResult("Stop before Start", passed);

    return passed;
}

bool TestJoin()
{
    ThreadManager manager;

    manager.Start(
        [](StopToken& token)
        {
            while (!token.IsStopRequested())
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }
        });

    manager.Stop();

    ThreadError result = manager.Join();

    bool passed = result == ThreadError::SUCCESS &&
                  !manager.IsRunning();

    PrintResult("Join", passed);

    return passed;
}

bool TestJoinBeforeStart()
{
    ThreadManager manager;

    ThreadError result = manager.Join();

    bool passed = result == ThreadError::NOT_RUNNING;

    PrintResult("Join before Start", passed);

    return passed;
}

bool TestDoubleJoin()
{
    ThreadManager manager;

    manager.Start(
        [](StopToken& token)
        {
            while (!token.IsStopRequested())
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }
        });

    manager.Stop();

    ThreadError first_join = manager.Join();
    ThreadError second_join = manager.Join();

    bool passed = first_join == ThreadError::SUCCESS &&
                  second_join == ThreadError::NOT_RUNNING;

    PrintResult("Double Join", passed);

    return passed;
}

bool TestNaturalWorkerCompletion()
{
    ThreadManager manager;

    ThreadError result = manager.Start(
        [](StopToken&)
        {
            // Worker completes naturally.
        });

    if (result != ThreadError::SUCCESS)
    {
        PrintResult(
            "Natural worker completion - Start",
            false);

        return false;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));

    result = manager.Join();

    bool passed = result == ThreadError::SUCCESS &&
                  !manager.IsRunning();

    PrintResult("Natural worker completion", passed);

    return passed;
}

bool TestRestartAfterJoin()
{
    ThreadManager manager;

    std::atomic<int> execution_count(0);

    auto worker = [&execution_count](StopToken& token)
    {
        execution_count.fetch_add(1);

        while (!token.IsStopRequested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    };

    ThreadError first_start = manager.Start(worker);

    if (first_start != ThreadError::SUCCESS)
    {
        PrintResult(
            "Restart after Join - First Start",
            false);

        return false;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));

    ThreadError first_stop = manager.Stop();

    if (first_stop != ThreadError::SUCCESS)
    {
        PrintResult(
            "Restart after Join - First Stop",
            false);

        return false;
    }

    ThreadError first_join = manager.Join();

    if (first_join != ThreadError::SUCCESS)
    {
        PrintResult(
            "Restart after Join - First Join",
            false);

        return false;
    }

    int first_execution_count = execution_count.load();

    ThreadError second_start = manager.Start(worker);

    if (second_start != ThreadError::SUCCESS)
    {
        PrintResult(
            "Restart after Join - Second Start",
            false);

        return false;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));

    ThreadError second_stop = manager.Stop();

    if (second_stop != ThreadError::SUCCESS)
    {
        PrintResult(
            "Restart after Join - Second Stop",
            false);

        return false;
    }

    ThreadError second_join = manager.Join();

    int second_execution_count = execution_count.load();

    bool passed = second_join == ThreadError::SUCCESS &&
                  !manager.IsRunning() &&
                  first_execution_count >= 1 &&
                  second_execution_count >= 2;

    PrintResult("Restart after Join", passed);

    return passed;
}

bool TestStopRequestsButJoinWaits()
{
    ThreadManager manager;

    std::atomic<bool> worker_started(false);
    std::atomic<bool> allow_worker_to_exit(false);

    ThreadError start_result = manager.Start(
        [&](StopToken& token)
        {
            worker_started.store(true);

            while (!token.IsStopRequested() ||
                   !allow_worker_to_exit.load())
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }
        });

    if (start_result != ThreadError::SUCCESS)
    {
        PrintResult(
            "Stop/Join semantics - Start",
            false);

        return false;
    }

    for (int i = 0; i < 100 && !worker_started.load(); ++i)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }

    if (!worker_started.load())
    {
        manager.Stop();
        allow_worker_to_exit.store(true);
        manager.Join();

        PrintResult(
            "Stop/Join semantics - Worker started",
            false);

        return false;
    }

    ThreadError stop_result = manager.Stop();

    bool still_running_after_stop = manager.IsRunning();

    allow_worker_to_exit.store(true);

    ThreadError join_result = manager.Join();

    bool stopped_after_join = !manager.IsRunning();

    bool passed = stop_result == ThreadError::SUCCESS &&
                  still_running_after_stop &&
                  join_result == ThreadError::SUCCESS &&
                  stopped_after_join;

    PrintResult(
        "Stop requests but Join waits",
        passed);

    return passed;
}

bool TestDoubleStop()
{
    ThreadManager manager;

    auto worker = [](StopToken& token)
    {
        while (!token.IsStopRequested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    };

    ThreadError start_result = manager.Start(worker);

    if (start_result != ThreadError::SUCCESS)
    {
        PrintResult("Double Stop - Start", false);
        return false;
    }

    ThreadError first_stop = manager.Stop();
    ThreadError second_stop = manager.Stop();

    manager.Join();

    bool passed = first_stop == ThreadError::SUCCESS &&
                  second_stop == ThreadError::SUCCESS;

    PrintResult("Double Stop", passed);

    return passed;
}

bool TestRestartAfterNaturalCompletion()
{
    ThreadManager manager;

    std::atomic<int> execution_count(0);

    auto worker = [&execution_count](StopToken&)
    {
        execution_count.fetch_add(1);
    };

    ThreadError first_start = manager.Start(worker);

    if (first_start != ThreadError::SUCCESS)
    {
        PrintResult(
            "Restart after natural completion - First Start",
            false);

        return false;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));

    ThreadError first_join = manager.Join();

    if (first_join != ThreadError::SUCCESS)
    {
        PrintResult(
            "Restart after natural completion - First Join",
            false);

        return false;
    }

    ThreadError second_start = manager.Start(worker);

    if (second_start != ThreadError::SUCCESS)
    {
        PrintResult(
            "Restart after natural completion - Second Start",
            false);

        return false;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(50));

    ThreadError second_join = manager.Join();

    bool passed = second_join == ThreadError::SUCCESS &&
                  !manager.IsRunning() &&
                  execution_count.load() >= 2;

    PrintResult(
        "Restart after natural completion",
        passed);

    return passed;
}

bool TestStartAfterStopBeforeJoin()
{
    ThreadManager manager;

    auto worker = [](StopToken& token)
    {
        while (!token.IsStopRequested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    };

    ThreadError first_start = manager.Start(worker);

    if (first_start != ThreadError::SUCCESS)
    {
        PrintResult(
            "Start after Stop before Join - First Start",
            false);

        return false;
    }

    ThreadError stop_result = manager.Stop();

    if (stop_result != ThreadError::SUCCESS)
    {
        PrintResult(
            "Start after Stop before Join - Stop",
            false);

        return false;
    }

    ThreadError second_start = manager.Start(worker);

    bool passed = second_start == ThreadError::ALREADY_RUNNING;

    manager.Join();

    PrintResult(
        "Start after Stop before Join",
        passed);

    return passed;
}

bool TestJoinWithoutStop()
{
    ThreadManager manager;

    std::atomic<bool> completed(false);

    ThreadError start_result = manager.Start(
        [&completed](StopToken&)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));

            completed.store(true);
        });

    if (start_result != ThreadError::SUCCESS)
    {
        PrintResult("Join without Stop - Start", false);
        return false;
    }

    ThreadError join_result = manager.Join();

    bool passed = join_result == ThreadError::SUCCESS &&
                  completed.load() &&
                  !manager.IsRunning();

    PrintResult("Join without Stop", passed);

    return passed;
}

bool TestConcurrentIsRunning()
{
    ThreadManager manager;

    auto worker = [](StopToken& token)
    {
        while (!token.IsStopRequested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    };

    ThreadError start_result = manager.Start(worker);

    if (start_result != ThreadError::SUCCESS)
    {
        PrintResult("Concurrent IsRunning - Start", false);
        return false;
    }

    std::atomic<bool> test_failed(false);

    std::thread reader1(
        [&manager, &test_failed]()
        {
            for (int i = 0; i < 10000; ++i)
            {
                (void)manager.IsRunning();
            }
        });

    std::thread reader2(
        [&manager, &test_failed]()
        {
            for (int i = 0; i < 10000; ++i)
            {
                (void)manager.IsRunning();
            }
        });

    reader1.join();
    reader2.join();

    manager.Stop();
    manager.Join();

    bool passed = !test_failed.load();

    PrintResult("Concurrent IsRunning", passed);

    return passed;
}

bool TestConcurrentStop()
{
    ThreadManager manager;

    auto worker = [](StopToken& token)
    {
        while (!token.IsStopRequested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    };

    ThreadError start_result = manager.Start(worker);

    if (start_result != ThreadError::SUCCESS)
    {
        PrintResult("Concurrent Stop - Start", false);
        return false;
    }

    ThreadError result1 = ThreadError::JOIN_FAILED;
    ThreadError result2 = ThreadError::JOIN_FAILED;

    std::thread stopper1(
        [&manager, &result1]()
        {
            result1 = manager.Stop();
        });

    std::thread stopper2(
        [&manager, &result2]()
        {
            result2 = manager.Stop();
        });

    stopper1.join();
    stopper2.join();

    manager.Join();

    bool passed = result1 == ThreadError::SUCCESS &&
                  result2 == ThreadError::SUCCESS;

    PrintResult("Concurrent Stop", passed);

    return passed;
}

bool TestConcurrentJoin()
{
    ThreadManager manager;

    auto worker = [](StopToken& token)
    {
        while (!token.IsStopRequested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    };

    ThreadError start_result = manager.Start(worker);

    if (start_result != ThreadError::SUCCESS)
    {
        PrintResult("Concurrent Join - Start", false);
        return false;
    }

    manager.Stop();

    ThreadError result1 = ThreadError::JOIN_FAILED;
    ThreadError result2 = ThreadError::JOIN_FAILED;

    std::thread joiner1(
        [&manager, &result1]()
        {
            result1 = manager.Join();
        });

    std::thread joiner2(
        [&manager, &result2]()
        {
            result2 = manager.Join();
        });

    joiner1.join();
    joiner2.join();

    bool valid_results =
        (result1 == ThreadError::SUCCESS &&
         result2 == ThreadError::NOT_RUNNING) ||
        (result2 == ThreadError::SUCCESS &&
         result1 == ThreadError::NOT_RUNNING);

    bool passed = valid_results &&
                  !manager.IsRunning();

    PrintResult("Concurrent Join", passed);

    return passed;
}

bool TestConcurrentStopAndJoin()
{
    ThreadManager manager;

    std::atomic<bool> worker_started(false);
    std::atomic<bool> stop_allowed(false);

    auto worker = [&](StopToken& token)
    {
        worker_started.store(true);

        while (!token.IsStopRequested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }

        stop_allowed.store(true);
    };

    ThreadError start_result = manager.Start(worker);

    if (start_result != ThreadError::SUCCESS)
    {
        PrintResult(
            "Concurrent Stop and Join - Start",
            false);

        return false;
    }

    for (int i = 0;
         i < 100 && !worker_started.load();
         ++i)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1));
    }

    if (!worker_started.load())
    {
        manager.Stop();
        manager.Join();

        PrintResult(
            "Concurrent Stop and Join - Worker started",
            false);

        return false;
    }

    /*
     * Ensure Stop() happens before Join() starts.
     */
    ThreadError stop_result = manager.Stop();

    if (stop_result != ThreadError::SUCCESS)
    {
        manager.Join();

        PrintResult(
            "Concurrent Stop and Join - Stop",
            false);

        return false;
    }

    ThreadError join_result = ThreadError::JOIN_FAILED;

    std::thread joiner(
        [&manager, &join_result]()
        {
            join_result = manager.Join();
        });

    joiner.join();

    bool passed = join_result == ThreadError::SUCCESS &&
                  !manager.IsRunning() &&
                  stop_allowed.load();

    PrintResult(
        "Concurrent Stop and Join",
        passed);

    return passed;
}

bool TestConcurrentStart()
{
    ThreadManager manager;

    auto worker = [](StopToken& token)
    {
        while (!token.IsStopRequested())
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }
    };

    ThreadError result1 = ThreadError::JOIN_FAILED;
    ThreadError result2 = ThreadError::JOIN_FAILED;

    std::thread starter1(
        [&manager, &worker, &result1]()
        {
            result1 = manager.Start(worker);
        });

    std::thread starter2(
        [&manager, &worker, &result2]()
        {
            result2 = manager.Start(worker);
        });

    starter1.join();
    starter2.join();

    bool one_success =
        (result1 == ThreadError::SUCCESS &&
         result2 == ThreadError::ALREADY_RUNNING) ||
        (result2 == ThreadError::SUCCESS &&
         result1 == ThreadError::ALREADY_RUNNING);

    /*
     * Cleanup whichever worker was successfully created.
     */
    if (manager.IsRunning())
    {
        manager.Stop();
        manager.Join();
    }

    bool passed = one_success &&
                  !manager.IsRunning();

    PrintResult("Concurrent Start", passed);

    return passed;
}

bool TestDestructorWithoutStart()
{
    {
        ThreadManager manager;
    }

    PrintResult("Destructor without Start", true);

    return true;
}

bool TestDestructorAfterNaturalCompletion()
{
    {
        ThreadManager manager;

        ThreadError result = manager.Start(
            [](StopToken&)
            {
                // Worker completes naturally.
            });

        if (result != ThreadError::SUCCESS)
        {
            PrintResult(
                "Destructor after natural completion - Start",
                false);

            return false;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(50));

        result = manager.Join();

        if (result != ThreadError::SUCCESS)
        {
            PrintResult(
                "Destructor after natural completion - Join",
                false);

            return false;
        }
    }

    PrintResult(
        "Destructor after natural completion",
        true);

    return true;
}

bool TestDestructorAfterJoin()
{
    {
        ThreadManager manager;

        ThreadError result = manager.Start(
            [](StopToken& token)
            {
                while (!token.IsStopRequested())
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(10));
                }
            });

        if (result != ThreadError::SUCCESS)
        {
            PrintResult(
                "Destructor after Join - Start",
                false);

            return false;
        }

        result = manager.Stop();

        if (result != ThreadError::SUCCESS)
        {
            PrintResult(
                "Destructor after Join - Stop",
                false);

            return false;
        }

        result = manager.Join();

        if (result != ThreadError::SUCCESS)
        {
            PrintResult(
                "Destructor after Join - Join",
                false);

            return false;
        }
    }

    PrintResult(
        "Destructor after Stop and Join",
        true);

    return true;
}

// bool TestDestructorWhileRunning()
// {
//     bool destroyed = false;

//     {
//         ThreadManager manager;

//         ThreadError result = manager.Start(
//             [](StopToken& token)
//             {
//                 while (!token.IsStopRequested())
//                 {
//                     std::this_thread::sleep_for(
//                         std::chrono::milliseconds(10));
//                 }
//             });

//         if (result != ThreadError::SUCCESS)
//         {
//             PrintResult(
//                 "Destructor while running - Start",
//                 false);

//             return false;
//         }

//         std::this_thread::sleep_for(
//             std::chrono::milliseconds(50));

//         /*
//          * Intentionally destroy ThreadManager while the
//          * worker is still running.
//          */
//     }

//     destroyed = true;

//     PrintResult(
//         "Destructor while running",
//         destroyed);

//     return destroyed;
// }

// bool TestDestructorAfterStopBeforeJoin()
// {
//     {
//         ThreadManager manager;

//         ThreadError result = manager.Start(
//             [](StopToken& token)
//             {
//                 while (!token.IsStopRequested())
//                 {
//                     std::this_thread::sleep_for(
//                         std::chrono::milliseconds(10));
//                 }
//             });

//         if (result != ThreadError::SUCCESS)
//         {
//             PrintResult(
//                 "Destructor after Stop before Join - Start",
//                 false);

//             return false;
//         }

//         result = manager.Stop();

//         if (result != ThreadError::SUCCESS)
//         {
//             PrintResult(
//                 "Destructor after Stop before Join - Stop",
//                 false);

//             return false;
//         }

//         /*
//          * Intentionally omit Join().
//          */
//     }

//     PrintResult(
//         "Destructor after Stop before Join",
//         true);

//     return true;
// }

bool TestRepeatedConstructionDestruction()
{
    for (int i = 0; i < 1000; ++i)
    {
        ThreadManager manager;
    }

    PrintResult(
        "Repeated construction/destruction",
        true);

    return true;
}

} // namespace

int main()
{
    bool all_tests_passed = true;

    all_tests_passed =
        TestInitialState() && all_tests_passed;

    all_tests_passed =
        TestInvalidWorker() && all_tests_passed;

    all_tests_passed =
        TestStart() && all_tests_passed;

    all_tests_passed =
        TestDoubleStart() && all_tests_passed;

    all_tests_passed =
        TestStop() && all_tests_passed;

    all_tests_passed =
        TestStopBeforeStart() && all_tests_passed;

    all_tests_passed =
        TestJoin() && all_tests_passed;

    all_tests_passed =
        TestJoinBeforeStart() && all_tests_passed;

    all_tests_passed =
        TestDoubleJoin() && all_tests_passed;

    all_tests_passed =
        TestNaturalWorkerCompletion() && all_tests_passed;

    all_tests_passed =
        TestRestartAfterJoin() && all_tests_passed;

    all_tests_passed =
        TestStopRequestsButJoinWaits() && all_tests_passed;

    all_tests_passed =
    TestDoubleStop() && all_tests_passed;

    all_tests_passed =
    TestRestartAfterNaturalCompletion() && all_tests_passed;

    all_tests_passed =
    TestStartAfterStopBeforeJoin() && all_tests_passed;

    all_tests_passed =
    TestJoinWithoutStop() && all_tests_passed;

    all_tests_passed =
    TestConcurrentIsRunning() && all_tests_passed;

    all_tests_passed =
    TestConcurrentStop() && all_tests_passed;

    all_tests_passed =
    TestConcurrentJoin() && all_tests_passed;

    all_tests_passed =
    TestConcurrentStopAndJoin() && all_tests_passed;

    all_tests_passed =
    TestConcurrentStart() && all_tests_passed;

    all_tests_passed =
    TestDestructorWithoutStart() && all_tests_passed;

    all_tests_passed =
    TestDestructorAfterNaturalCompletion() &&
    all_tests_passed;

    all_tests_passed =
    TestDestructorAfterJoin() && all_tests_passed;

    // all_tests_passed =
    // TestDestructorWhileRunning() && all_tests_passed;

    // all_tests_passed =
    // TestDestructorAfterStopBeforeJoin() &&
    // all_tests_passed;

    all_tests_passed =
    TestRepeatedConstructionDestruction() &&
    all_tests_passed;

    if (all_tests_passed)
    {
        std::cout
            << "ALL THREAD MANAGER TESTS PASSED\n";

        return 0;
    }

    std::cerr
        << "THREAD MANAGER TESTS FAILED\n";

    return 1;
}