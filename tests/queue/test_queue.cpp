#include "framework/queue/queue.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <utility>

namespace
{

int tests_run = 0;
int tests_passed = 0;

void Check(bool condition, const std::string& test_name)
{
    ++tests_run;

    if (condition)
    {
        ++tests_passed;
        std::cout << "[PASS] " << test_name << '\n';
    }
    else
    {
        std::cout << "[FAIL] " << test_name << '\n';
    }
}

/*
 * Test 1:
 * A positive capacity creates a valid running queue.
 */
void TestValidConstruction()
{
    Queue<int> queue(5);

    Check(queue.IsValid(), "Valid construction");
    Check(!queue.IsShutdown(), "New queue is not shutdown");
    Check(queue.Size() == 0, "New queue is empty");
    Check(queue.Capacity() == 5, "Capacity is correct");
}

/*
 * Test 2:
 * Capacity zero is intentionally represented as an invalid,
 * terminal queue because the constructor cannot return an error.
 */
void TestInvalidConstruction()
{
    Queue<int> queue(0);

    Check(!queue.IsValid(), "Zero capacity queue is invalid");
    Check(queue.IsShutdown(), "Zero capacity queue is shutdown");
    Check(queue.Size() == 0, "Invalid queue is empty");

    int value = 10;

    Check(
        queue.Push(std::move(value)) == QueueError::QUEUE_SHUTDOWN,
        "Push to invalid queue is rejected"
    );
}

/*
 * Test 3:
 * Basic move-based Push and Pop.
 */
void TestBasicPushPop()
{
    Queue<int> queue(5);

    int input = 42;

    QueueError push_result = queue.Push(std::move(input));

    int output = 0;

    QueueError pop_result = queue.Pop(output);

    Check(push_result == QueueError::SUCCESS, "Basic Push succeeds");
    Check(pop_result == QueueError::SUCCESS, "Basic Pop succeeds");
    Check(output == 42, "Popped value is correct");
    Check(queue.Size() == 0, "Queue empty after Pop");
}

/*
 * Test 4:
 * FIFO ordering is fundamental to std::queue-backed Queue.
 */
void TestFIFO()
{
    Queue<int> queue(5);

    int first = 10;
    int second = 20;
    int third = 30;

    queue.Push(std::move(first));
    queue.Push(std::move(second));
    queue.Push(std::move(third));

    int value1 = 0;
    int value2 = 0;
    int value3 = 0;

    QueueError result1 = queue.Pop(value1);
    QueueError result2 = queue.Pop(value2);
    QueueError result3 = queue.Pop(value3);

    Check(result1 == QueueError::SUCCESS, "FIFO first Pop succeeds");
    Check(result2 == QueueError::SUCCESS, "FIFO second Pop succeeds");
    Check(result3 == QueueError::SUCCESS, "FIFO third Pop succeeds");

    Check(value1 == 10, "FIFO first value");
    Check(value2 == 20, "FIFO second value");
    Check(value3 == 30, "FIFO third value");
}

/*
 * Test 5:
 * Queue capacity must be enforced.
 */
void TestQueueFull()
{
    Queue<int> queue(2);

    int first = 1;
    int second = 2;
    int third = 3;

    Check(
        queue.Push(std::move(first)) == QueueError::SUCCESS,
        "First Push into bounded queue"
    );

    Check(
        queue.Push(std::move(second)) == QueueError::SUCCESS,
        "Second Push into bounded queue"
    );

    Check(
        queue.Push(std::move(third)) == QueueError::QUEUE_FULL,
        "Push when queue is full"
    );

    Check(queue.Size() == 2, "Full queue size equals capacity");

    int value = 0;

    queue.Pop(value);

    Check(value == 1, "Full queue retains FIFO contents");

    Check(
        queue.Size() == 1,
        "Queue size decreases after Pop"
    );

    int fourth = 4;

    Check(
        queue.Push(std::move(fourth)) == QueueError::SUCCESS,
        "Push succeeds after capacity becomes available"
    );
}

/*
 * Test 6:
 * A failed Pop must not modify the caller's output object.
 *
 * The Pop is tested in a separate thread because Pop is intentionally
 * blocking while the queue is empty.
 */
void TestBlockedPop()
{
    Queue<int> queue(5);

    std::atomic<bool> pop_started(false);
    std::atomic<bool> pop_finished(false);

    int result = 12345;

    std::thread consumer(
        [&]()
        {
            pop_started.store(true);

            QueueError error = queue.Pop(result);

            pop_finished.store(
                error == QueueError::SUCCESS && result == 99
            );
        }
    );

    while (!pop_started.load())
    {
        std::this_thread::yield();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Check(
        !pop_finished.load(),
        "Pop blocks while queue is empty"
    );

    int value = 99;

    Check(
        queue.Push(std::move(value)) == QueueError::SUCCESS,
        "Push wakes blocked Pop"
    );

    consumer.join();

    Check(
        pop_finished.load(),
        "Blocked Pop receives pushed item"
    );

    Check(
        queue.Size() == 0,
        "Queue empty after blocked Pop completes"
    );
}

/*
 * Test 7:
 * IMMEDIATE shutdown discards all queued work.
 */
void TestImmediateShutdown()
{
    Queue<int> queue(5);

    int first = 1;
    int second = 2;
    int third = 3;

    queue.Push(std::move(first));
    queue.Push(std::move(second));
    queue.Push(std::move(third));

    Check(
        queue.Size() == 3,
        "Immediate shutdown setup contains queued items"
    );

    Check(
        queue.Shutdown(QueueShutdownMode::IMMEDIATE)
            == QueueError::SUCCESS,
        "Immediate Shutdown succeeds"
    );

    Check(
        queue.IsShutdown(),
        "Immediate Shutdown enters shutdown state"
    );

    Check(
        queue.Size() == 0,
        "Immediate Shutdown discards queued items"
    );

    int value = 100;

    Check(
        queue.Pop(value) == QueueError::QUEUE_SHUTDOWN,
        "Pop after Immediate Shutdown returns shutdown"
    );

    Check(
        value == 100,
        "Failed Pop after shutdown does not modify output"
    );

    int new_value = 4;

    Check(
        queue.Push(std::move(new_value))
            == QueueError::QUEUE_SHUTDOWN,
        "Push after Immediate Shutdown is rejected"
    );
}

/*
 * Test 8:
 * Repeated shutdown is rejected and does not alter terminal state.
 */
void TestRepeatedImmediateShutdown()
{
    Queue<int> queue(5);

    Check(
        queue.Shutdown(QueueShutdownMode::IMMEDIATE)
            == QueueError::SUCCESS,
        "First Immediate Shutdown succeeds"
    );

    Check(
        queue.Shutdown(QueueShutdownMode::IMMEDIATE)
            == QueueError::QUEUE_SHUTDOWN,
        "Repeated Immediate Shutdown is rejected"
    );

    Check(queue.IsShutdown(), "Repeated shutdown preserves terminal state");
}

/*
 * Test 9:
 * DRAIN shutdown rejects producers but allows existing items to drain.
 */
void TestDrainShutdown()
{
    Queue<int> queue(5);

    int first = 10;
    int second = 20;
    int third = 30;

    queue.Push(std::move(first));
    queue.Push(std::move(second));
    queue.Push(std::move(third));

    Check(
        queue.Shutdown(QueueShutdownMode::DRAIN)
            == QueueError::SUCCESS,
        "Drain Shutdown succeeds"
    );

    /*
     * DRAIN is not yet terminal because items still exist.
     */
    Check(
        !queue.IsShutdown(),
        "Drain state is not immediately terminal when items remain"
    );

    int rejected = 40;

    Check(
        queue.Push(std::move(rejected))
            == QueueError::QUEUE_SHUTDOWN,
        "Push rejected while draining"
    );

    int value1 = 0;
    int value2 = 0;
    int value3 = 0;

    Check(
        queue.Pop(value1) == QueueError::SUCCESS,
        "Drain Pop first item"
    );

    Check(
        queue.Pop(value2) == QueueError::SUCCESS,
        "Drain Pop second item"
    );

    Check(
        queue.Pop(value3) == QueueError::SUCCESS,
        "Drain Pop final item"
    );

    Check(value1 == 10, "Drain preserves first FIFO item");
    Check(value2 == 20, "Drain preserves second FIFO item");
    Check(value3 == 30, "Drain preserves third FIFO item");

    Check(
        queue.IsShutdown(),
        "Drain becomes shutdown after final item"
    );

    int value4 = 0;

    Check(
        queue.Pop(value4) == QueueError::QUEUE_SHUTDOWN,
        "Pop after drained queue returns shutdown"
    );
}

/*
 * Test 10:
 * DRAIN on an already empty queue should become terminal immediately.
 */
void TestDrainEmptyQueue()
{
    Queue<int> queue(5);

    Check(
        queue.Shutdown(QueueShutdownMode::DRAIN)
            == QueueError::SUCCESS,
        "Drain Shutdown on empty queue succeeds"
    );

    Check(
        queue.IsShutdown(),
        "Empty queue enters shutdown immediately after Drain"
    );

    int value = 123;

    Check(
        queue.Pop(value) == QueueError::QUEUE_SHUTDOWN,
        "Pop from empty drained queue returns shutdown"
    );

    Check(
        value == 123,
        "Failed Pop does not modify output"
    );
}

/*
 * Test 11:
 * A blocked consumer must wake when IMMEDIATE shutdown occurs.
 */
void TestBlockedConsumerImmediateShutdown()
{
    Queue<int> queue(5);

    std::atomic<bool> finished(false);
    QueueError result = QueueError::SUCCESS;

    std::thread consumer(
        [&]()
        {
            int value = 0;

            result = queue.Pop(value);
            finished.store(true);
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Check(
        !finished.load(),
        "Consumer is blocked before shutdown"
    );

    Check(
        queue.Shutdown(QueueShutdownMode::IMMEDIATE)
            == QueueError::SUCCESS,
        "Immediate Shutdown wakes blocked consumer"
    );

    consumer.join();

    Check(
        finished.load(),
        "Blocked consumer exits after Immediate Shutdown"
    );

    Check(
        result == QueueError::QUEUE_SHUTDOWN,
        "Blocked consumer receives shutdown result"
    );
}

/*
 * Test 12:
 * Multiple blocked consumers must all be released by shutdown.
 */
void TestMultipleBlockedConsumers()
{
    Queue<int> queue(5);

    constexpr int consumer_count = 8;

    std::atomic<int> completed(0);

    std::vector<std::thread> consumers;

    consumers.reserve(consumer_count);

    for (int i = 0; i < consumer_count; ++i)
    {
        consumers.emplace_back(
            [&]()
            {
                int value = 0;

                QueueError result = queue.Pop(value);

                if (result == QueueError::QUEUE_SHUTDOWN)
                    completed.fetch_add(1);
            }
        );
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Check(
        completed.load() == 0,
        "Multiple consumers remain blocked before shutdown"
    );

    queue.Shutdown(QueueShutdownMode::IMMEDIATE);

    for (std::thread& consumer : consumers)
        consumer.join();

    Check(
        completed.load() == consumer_count,
        "All blocked consumers wake on shutdown"
    );
}

/*
 * Test 13:
 * Multiple consumers should correctly divide queued work.
 */
void TestMultipleConsumers()
{
    Queue<int> queue(100);

    constexpr int item_count = 1000;
    constexpr int consumer_count = 4;

    std::atomic<int> consumed_count(0);
    std::atomic<long long> sum(0);

    std::vector<std::thread> consumers;

    consumers.reserve(consumer_count);

    for (int i = 0; i < consumer_count; ++i)
    {
        consumers.emplace_back(
            [&]()
            {
                while (true)
                {
                    int value = 0;

                    QueueError result = queue.Pop(value);

                    if (result == QueueError::QUEUE_SHUTDOWN)
                        break;

                    sum.fetch_add(value);
                    consumed_count.fetch_add(1);
                }
            }
        );
    }

    for (int i = 1; i <= item_count; ++i)
    {
        int value = i;

        while (queue.Push(std::move(value))
               == QueueError::QUEUE_FULL)
        {
            std::this_thread::yield();
        }
    }

    queue.Shutdown(QueueShutdownMode::DRAIN);

    for (std::thread& consumer : consumers)
        consumer.join();

    const long long expected_sum =
        static_cast<long long>(item_count)
        * static_cast<long long>(item_count + 1)
        / 2;

    Check(
        consumed_count.load() == item_count,
        "Multiple consumers consume every item exactly once"
    );

    Check(
        sum.load() == expected_sum,
        "Multiple consumers preserve total data"
    );
}

/*
 * Test 14:
 * Multiple producers concurrently pushing into one queue.
 */
void TestMultipleProducers()
{
    Queue<int> queue(100);

    constexpr int producer_count = 4;
    constexpr int items_per_producer = 250;

    std::atomic<int> successful_pushes(0);

    std::vector<std::thread> producers;

    producers.reserve(producer_count);

    for (int producer_id = 0;
         producer_id < producer_count;
         ++producer_id)
    {
        producers.emplace_back(
            [&queue, &successful_pushes, producer_id]()
            {
                for (int i = 0; i < items_per_producer; ++i)
                {
                    int value = producer_id * 10000 + i;

                    while (true)
                    {
                        QueueError result =
                            queue.Push(std::move(value));

                        if (result == QueueError::SUCCESS)
                        {
                            successful_pushes.fetch_add(1);
                            break;
                        }

                        if (result == QueueError::QUEUE_SHUTDOWN)
                            return;

                        std::this_thread::yield();
                    }
                }
            }
        );
    }

    std::atomic<int> consumed(0);

    std::thread consumer(
        [&]()
        {
            while (consumed.load()
                   < producer_count * items_per_producer)
            {
                int value = 0;

                QueueError result = queue.Pop(value);

                if (result == QueueError::SUCCESS)
                {
                    consumed.fetch_add(1);
                }
                else
                {
                    return;
                }
            }
        }
    );

    for (std::thread& producer : producers)
        producer.join();

    while (consumed.load()
           < producer_count * items_per_producer)
    {
        std::this_thread::yield();
    }

    queue.Shutdown(QueueShutdownMode::IMMEDIATE);

    consumer.join();

    Check(
        successful_pushes.load()
            == producer_count * items_per_producer,
        "Multiple producers successfully push all items"
    );

    Check(
        consumed.load()
            == producer_count * items_per_producer,
        "Multiple producers' items are all consumed"
    );
}

/*
 * Test 15:
 * Concurrent producer/consumer operation under sustained load.
 */
void TestProducerConsumerStress()
{
    Queue<int> queue(64);

    constexpr int producer_count = 4;
    constexpr int consumer_count = 4;
    constexpr int items_per_producer = 2000;

    constexpr int total_items =
        producer_count * items_per_producer;

    std::atomic<int> produced(0);
    std::atomic<int> consumed(0);

    std::vector<std::thread> consumers;

    consumers.reserve(consumer_count);

    for (int i = 0; i < consumer_count; ++i)
    {
        consumers.emplace_back(
            [&]()
            {
                while (true)
                {
                    int value = 0;

                    QueueError result = queue.Pop(value);

                    if (result == QueueError::SUCCESS)
                    {
                        consumed.fetch_add(1);
                    }
                    else
                    {
                        break;
                    }
                }
            }
        );
    }

    std::vector<std::thread> producers;

    producers.reserve(producer_count);

    for (int producer_id = 0;
         producer_id < producer_count;
         ++producer_id)
    {
        producers.emplace_back(
            [&queue, &produced, producer_id]()
            {
                for (int i = 0; i < items_per_producer; ++i)
                {
                    int value = producer_id * 100000 + i;

                    while (true)
                    {
                        QueueError result =
                            queue.Push(std::move(value));

                        if (result == QueueError::SUCCESS)
                        {
                            produced.fetch_add(1);
                            break;
                        }

                        if (result == QueueError::QUEUE_SHUTDOWN)
                            return;

                        std::this_thread::yield();
                    }
                }
            }
        );
    }

    for (std::thread& producer : producers)
        producer.join();

    Check(
        produced.load() == total_items,
        "Stress producers generate all expected items"
    );

    queue.Shutdown(QueueShutdownMode::DRAIN);

    for (std::thread& consumer : consumers)
        consumer.join();

    Check(
        consumed.load() == total_items,
        "Stress consumers consume all expected items"
    );

    Check(
        queue.IsShutdown(),
        "Stress queue reaches shutdown after drain"
    );
}

/*
 * Test 16:
 * Concurrent Shutdown calls must be serialized safely.
 *
 * Exactly one caller should perform the transition successfully;
 * the others should observe terminal shutdown.
 */
void TestConcurrentShutdown()
{
    Queue<int> queue(10);

    constexpr int thread_count = 8;

    std::atomic<int> successful_shutdowns(0);
    std::atomic<int> rejected_shutdowns(0);

    std::vector<std::thread> threads;

    threads.reserve(thread_count);

    for (int i = 0; i < thread_count; ++i)
    {
        threads.emplace_back(
            [&]()
            {
                QueueError result =
                    queue.Shutdown(QueueShutdownMode::IMMEDIATE);

                if (result == QueueError::SUCCESS)
                    successful_shutdowns.fetch_add(1);
                else if (result == QueueError::QUEUE_SHUTDOWN)
                    rejected_shutdowns.fetch_add(1);
            }
        );
    }

    for (std::thread& thread : threads)
        thread.join();

    Check(
        successful_shutdowns.load() == 1,
        "Exactly one concurrent Shutdown succeeds"
    );

    Check(
        rejected_shutdowns.load() == thread_count - 1,
        "Remaining concurrent Shutdown calls are rejected"
    );

    Check(
        queue.IsShutdown(),
        "Concurrent Shutdown leaves queue terminal"
    );
}

/*
 * Test 17:
 * Concurrent Size/IsShutdown calls must be safe.
 */
void TestConcurrentObservers()
{
    Queue<int> queue(100);

    constexpr int observer_count = 8;
    constexpr int iterations = 10000;

    std::atomic<bool> failed(false);

    std::vector<std::thread> observers;

    observers.reserve(observer_count);

    for (int i = 0; i < observer_count; ++i)
    {
        observers.emplace_back(
            [&]()
            {
                for (int j = 0; j < iterations; ++j)
                {
                    std::size_t size = queue.Size();
                    bool shutdown = queue.IsShutdown();
                    std::size_t capacity = queue.Capacity();

                    if (size > capacity && !shutdown)
                        failed.store(true);
                }
            }
        );
    }

    for (std::thread& observer : observers)
        observer.join();

    Check(
        !failed.load(),
        "Concurrent Size/IsShutdown observers"
    );
}

/*
 * Test 18:
 * A queue must be reusable only by creating a new Queue object.
 *
 * Shutdown is deliberately one-way. There is no Restart API.
 */
void TestShutdownIsOneWay()
{
    Queue<int> queue(5);

    queue.Shutdown(QueueShutdownMode::IMMEDIATE);

    int value = 1;

    Check(
        queue.Push(std::move(value))
            == QueueError::QUEUE_SHUTDOWN,
        "Shutdown cannot be reversed"
    );

    Check(
        queue.IsShutdown(),
        "Queue remains shutdown"
    );
}

/*
 * Test 19:
 * Move-only payload support is important for firmware objects that
 * cannot or should not be copied.
 */
struct MoveOnly
{
    int value;

    explicit MoveOnly(int value)
        : value(value)
    {
    }

    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;

    MoveOnly(MoveOnly&& other) noexcept
        : value(other.value)
    {
        other.value = -1;
    }

    MoveOnly& operator=(MoveOnly&& other) noexcept
    {
        if (this != &other)
        {
            value = other.value;
            other.value = -1;
        }

        return *this;
    }
};

void TestMoveOnlyPayload()
{
    Queue<MoveOnly> queue(2);

    MoveOnly input(123);

    Check(
        queue.Push(std::move(input)) == QueueError::SUCCESS,
        "Move-only payload Push"
    );

    MoveOnly output(0);

    Check(
        queue.Pop(output) == QueueError::SUCCESS,
        "Move-only payload Pop"
    );

    Check(
        output.value == 123,
        "Move-only payload preserves value"
    );
}

/*
 * Test 20:
 * Drain shutdown must correctly wake consumers when there are no
 * queued items.
 */
void TestBlockedConsumerDrainShutdown()
{
    Queue<int> queue(5);

    std::atomic<bool> finished(false);
    QueueError result = QueueError::SUCCESS;

    std::thread consumer(
        [&]()
        {
            int value = 0;

            result = queue.Pop(value);
            finished.store(true);
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    Check(
        !finished.load(),
        "Consumer blocks before Drain Shutdown"
    );

    Check(
        queue.Shutdown(QueueShutdownMode::DRAIN)
            == QueueError::SUCCESS,
        "Drain Shutdown wakes blocked empty consumer"
    );

    consumer.join();

    Check(
        finished.load(),
        "Blocked consumer exits after Drain Shutdown"
    );

    Check(
        result == QueueError::QUEUE_SHUTDOWN,
        "Blocked consumer receives shutdown from Drain"
    );

    Check(
        queue.IsShutdown(),
        "Empty Drain queue becomes terminal"
    );
}

} // namespace

int main()
{
    std::cout << "========================================\n";
    std::cout << " Queue Framework Test Suite\n";
    std::cout << "========================================\n\n";

    TestValidConstruction();
    TestInvalidConstruction();
    TestBasicPushPop();
    TestFIFO();
    TestQueueFull();
    TestBlockedPop();
    TestImmediateShutdown();
    TestRepeatedImmediateShutdown();
    TestDrainShutdown();
    TestDrainEmptyQueue();
    TestBlockedConsumerImmediateShutdown();
    TestMultipleBlockedConsumers();
    TestMultipleConsumers();
    TestMultipleProducers();
    TestProducerConsumerStress();
    TestConcurrentShutdown();
    TestConcurrentObservers();
    TestShutdownIsOneWay();
    TestMoveOnlyPayload();
    TestBlockedConsumerDrainShutdown();

    std::cout << "\n========================================\n";
    std::cout << " Tests Run    : " << tests_run << '\n';
    std::cout << " Tests Passed : " << tests_passed << '\n';
    std::cout << " Tests Failed : "
              << (tests_run - tests_passed) << '\n';
    std::cout << "========================================\n";

    if (tests_run == tests_passed)
    {
        std::cout << "ALL QUEUE TESTS PASSED\n";
        return 0;
    }

    std::cout << "QUEUE TESTS FAILED\n";
    return 1;
}