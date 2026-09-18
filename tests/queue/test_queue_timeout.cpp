#include "framework/queue/queue.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{

int tests_run    = 0;
int tests_passed = 0;

void Check(bool condition, const std::string& name)
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

using namespace std::chrono_literals;

// ------------------------------------------------------------------
// PushFor tests
// ------------------------------------------------------------------

void TestPushForWhenSpaceAvailable()
{
    Queue<int> queue(4);

    int v = 7;
    auto result = queue.PushFor(std::move(v), 100ms);

    Check(result == QueueError::SUCCESS,
          "PushFor succeeds when space is available");
    Check(queue.Size() == 1,
          "PushFor added item");
}

void TestPushForTimesOutWhenFull()
{
    Queue<int> queue(2);

    int a = 1, b = 2, c = 3;
    queue.Push(std::move(a));
    queue.Push(std::move(b));

    const auto start = std::chrono::steady_clock::now();
    auto result = queue.PushFor(std::move(c), 150ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    Check(result == QueueError::QUEUE_TIMEOUT,
          "PushFor returns QUEUE_TIMEOUT when full");
    Check(queue.Size() == 2,
          "Full queue unchanged after PushFor timeout");
    Check(elapsed >= 100ms,
          "PushFor waited approximately the requested time");
    Check(elapsed < 500ms,
          "PushFor did not wait significantly longer than requested");
}

void TestPushForWakesOnConsumerPop()
{
    Queue<int> queue(1);

    int a = 1;
    queue.Push(std::move(a));

    std::atomic<bool> push_result_received(false);
    QueueError push_result = QueueError::SUCCESS;
    int pushed_value = 99;

    std::thread producer(
        [&]()
        {
            push_result = queue.PushFor(std::move(pushed_value),
                                        2000ms);
            push_result_received.store(true);
        });

    std::this_thread::sleep_for(50ms);

    Check(!push_result_received.load(),
          "PushFor blocks while queue is full");

    int popped = 0;
    queue.Pop(popped);

    producer.join();

    Check(push_result_received.load(),
          "PushFor unblocked after Pop made space");
    Check(push_result == QueueError::SUCCESS,
          "PushFor succeeded after consumer made space");
    Check(queue.Size() == 1,
          "Queue has one item after producer succeeds");
}

void TestPushForShutdownWhileWaiting()
{
    Queue<int> queue(1);

    int a = 1;
    queue.Push(std::move(a));

    QueueError result = QueueError::QUEUE_TIMEOUT;

    std::thread producer(
        [&]()
        {
            int v = 42;
            result = queue.PushFor(std::move(v), 5000ms);
        });

    std::this_thread::sleep_for(50ms);

    queue.Shutdown(QueueShutdownMode::IMMEDIATE);

    producer.join();

    Check(result == QueueError::QUEUE_SHUTDOWN,
          "PushFor returns QUEUE_SHUTDOWN when shutdown while waiting");
}

void TestPushForOnShutdownQueue()
{
    Queue<int> queue(4);
    queue.Shutdown(QueueShutdownMode::IMMEDIATE);

    int v = 1;
    auto result = queue.PushFor(std::move(v), 100ms);

    Check(result == QueueError::QUEUE_SHUTDOWN,
          "PushFor on shutdown queue returns QUEUE_SHUTDOWN");
}

// ------------------------------------------------------------------
// PopFor tests
// ------------------------------------------------------------------

void TestPopForWhenDataAvailable()
{
    Queue<int> queue(4);

    int v = 42;
    queue.Push(std::move(v));

    int out = 0;
    auto result = queue.PopFor(out, 100ms);

    Check(result == QueueError::SUCCESS,
          "PopFor succeeds when data available");
    Check(out == 42,
          "PopFor returns correct value");
    Check(queue.Size() == 0,
          "PopFor removed item");
}

void TestPopForTimesOutWhenEmpty()
{
    Queue<int> queue(4);

    int out = 999;

    const auto start = std::chrono::steady_clock::now();
    auto result = queue.PopFor(out, 150ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    Check(result == QueueError::QUEUE_TIMEOUT,
          "PopFor returns QUEUE_TIMEOUT on empty queue");
    Check(out == 999,
          "PopFor timeout does not modify output value");
    Check(elapsed >= 100ms,
          "PopFor waited approximately the requested time");
    Check(elapsed < 500ms,
          "PopFor did not wait significantly longer than requested");
}

void TestPopForWakesOnPush()
{
    Queue<int> queue(4);

    QueueError pop_result = QueueError::QUEUE_TIMEOUT;
    int popped_value = 0;
    std::atomic<bool> pop_finished(false);

    std::thread consumer(
        [&]()
        {
            pop_result = queue.PopFor(popped_value, 5000ms);
            pop_finished.store(true);
        });

    std::this_thread::sleep_for(50ms);

    Check(!pop_finished.load(),
          "PopFor blocks while queue is empty");

    int v = 123;
    queue.Push(std::move(v));

    consumer.join();

    Check(pop_finished.load(),
          "PopFor unblocked after Push");
    Check(pop_result == QueueError::SUCCESS,
          "PopFor succeeded after Push");
    Check(popped_value == 123,
          "PopFor received correct value");
}

void TestPopForShutdownImmediate()
{
    Queue<int> queue(4);

    QueueError result = QueueError::QUEUE_TIMEOUT;
    int out = 0;

    std::thread consumer(
        [&]()
        {
            result = queue.PopFor(out, 5000ms);
        });

    std::this_thread::sleep_for(50ms);

    queue.Shutdown(QueueShutdownMode::IMMEDIATE);

    consumer.join();

    Check(result == QueueError::QUEUE_SHUTDOWN,
          "PopFor returns QUEUE_SHUTDOWN on immediate shutdown");
}

void TestPopForShutdownDrainWithData()
{
    Queue<int> queue(4);

    int a = 100;
    queue.Push(std::move(a));

    queue.Shutdown(QueueShutdownMode::DRAIN);

    int out = 0;
    auto result = queue.PopFor(out, 100ms);

    Check(result == QueueError::SUCCESS,
          "PopFor drains existing item");
    Check(out == 100,
          "PopFor returns drained value");

    int next = 0;
    auto after_drain = queue.PopFor(next, 100ms);

    Check(after_drain == QueueError::QUEUE_SHUTDOWN,
          "PopFor returns shutdown after drain completes");
    Check(queue.IsShutdown(),
          "Queue becomes terminal after drain completes");
}

void TestPopForPeriodicPollingPattern()
{
    // Simulates a service that wakes periodically to do other work
    // while draining a queue.
    Queue<int> queue(4);

    int items_produced = 0;
    int items_consumed = 0;
    int periodic_ticks  = 0;

    std::atomic<bool> stop{false};

    std::thread producer(
        [&]()
        {
            for (int i = 0; i < 5; ++i)
            {
                std::this_thread::sleep_for(30ms);

                int v = i;
                if (queue.Push(std::move(v)) ==
                    QueueError::SUCCESS)
                {
                    ++items_produced;
                }
            }
            stop.store(true);
        });

    while (!stop.load() || queue.Size() > 0)
    {
        int out = 0;
        auto result = queue.PopFor(out, 20ms);

        if (result == QueueError::SUCCESS)
        {
            ++items_consumed;
        }
        else if (result == QueueError::QUEUE_TIMEOUT)
        {
            ++periodic_ticks;
        }
        else
        {
            break;
        }
    }

    producer.join();

    Check(items_produced == 5,
          "Periodic polling pattern: all items produced");
    Check(items_consumed == 5,
          "Periodic polling pattern: all items consumed");
    Check(periodic_ticks > 0,
          "Periodic polling pattern: consumer woke on timeout at least once");
}

// ------------------------------------------------------------------
// Backpressure / producer-consumer with timed operations
// ------------------------------------------------------------------

void TestProducerBackpressureWithPushFor()
{
    Queue<int> queue(2);

    constexpr int producer_count = 2;
    constexpr int items_per_producer = 50;

    std::atomic<int> produced(0);
    std::atomic<int> consumed(0);

    std::vector<std::thread> producers;
    for (int p = 0; p < producer_count; ++p)
    {
        producers.emplace_back([&, p]()
        {
            for (int i = 0; i < items_per_producer; ++i)
            {
                int v = p * 1000 + i;
                while (queue.PushFor(std::move(v), 500ms) ==
                       QueueError::SUCCESS)
                {
                    produced.fetch_add(1);
                    break;
                }
            }
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < 2; ++c)
    {
        consumers.emplace_back([&]()
        {
            while (true)
            {
                int v = 0;
                auto r = queue.PopFor(v, 200ms);
                if (r == QueueError::SUCCESS)
                {
                    consumed.fetch_add(1);
                }
                else if (r == QueueError::QUEUE_TIMEOUT)
                {
                    if (produced.load() == producer_count * items_per_producer
                        && consumed.load() == produced.load())
                        break;
                }
                else
                {
                    break;
                }
            }
        });
    }

    for (auto& t : producers) t.join();

    while (consumed.load() < producer_count * items_per_producer)
        std::this_thread::yield();

    queue.Shutdown(QueueShutdownMode::DRAIN);
    for (auto& t : consumers) t.join();

    Check(produced.load() == producer_count * items_per_producer,
          "Timed producer/consumer: all items produced");
    Check(consumed.load() == producer_count * items_per_producer,
          "Timed producer/consumer: all items consumed");
}

} // namespace

int main()
{
    std::cout << "========================================\n";
    std::cout << " Queue Timeout API Test Suite\n";
    std::cout << "========================================\n\n";

    TestPushForWhenSpaceAvailable();
    TestPushForTimesOutWhenFull();
    TestPushForWakesOnConsumerPop();
    TestPushForShutdownWhileWaiting();
    TestPushForOnShutdownQueue();

    TestPopForWhenDataAvailable();
    TestPopForTimesOutWhenEmpty();
    TestPopForWakesOnPush();
    TestPopForShutdownImmediate();
    TestPopForShutdownDrainWithData();
    TestPopForPeriodicPollingPattern();

    TestProducerBackpressureWithPushFor();

    std::cout << "\n========================================\n";
    std::cout << " Tests Run    : " << tests_run << '\n';
    std::cout << " Tests Passed : " << tests_passed << '\n';
    std::cout << " Tests Failed : "
              << (tests_run - tests_passed) << '\n';
    std::cout << "========================================\n";

    if (tests_run == tests_passed)
    {
        std::cout << "ALL QUEUE TIMEOUT TESTS PASSED\n";
        return 0;
    }

    std::cout << "QUEUE TIMEOUT TESTS FAILED\n";
    return 1;
}