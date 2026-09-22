#include "framework/event/event_bus.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

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

// ----- Event types used by tests -----

struct EventA { int value = 0; };
struct EventB { std::string text; };
struct EventC { double x = 0.0; };

// ----- Thread-safe counter -----

struct Counter
{
    std::mutex              m;
    std::condition_variable cv;
    int                     value = 0;

    void Increment()
    {
        std::lock_guard<std::mutex> lock(m);
        ++value;
        cv.notify_all();
    }

    bool WaitForAtLeast(int target, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m);
        return cv.wait_for(lock, timeout, [&] { return value >= target; });
    }

    int Get()
    {
        std::lock_guard<std::mutex> lock(m);
        return value;
    }
};

// ================= Basic delivery =================

void TestPublishWithNoSubscribers()
{
    EventBus bus;
    auto r = bus.Publish(EventA{1});
    Check(r == EventError::SUCCESS,
          "Publish with no subscribers succeeds");
}

void TestBasicSubscribeAndPublish()
{
    EventBus bus;
    int received = -1;

    auto sub = bus.Subscribe<EventA>(
        [&received](const EventA& e) { received = e.value; });

    Check(sub.IsValid(), "Subscription is valid");

    auto r = bus.Publish(EventA{42});

    Check(r == EventError::SUCCESS, "Publish succeeds");
    Check(received == 42, "Subscriber received the value");
}

void TestMultipleSubscribers()
{
    EventBus bus;
    int a = 0, b = 0, c = 0;

    auto s1 = bus.Subscribe<EventA>([&a](const EventA&) { a = 1; });
    auto s2 = bus.Subscribe<EventA>([&b](const EventA&) { b = 1; });
    auto s3 = bus.Subscribe<EventA>([&c](const EventA&) { c = 1; });

    bus.Publish(EventA{});

    Check(a == 1 && b == 1 && c == 1,
          "All subscribers fire for one publish");
}

void TestTypeIsolation()
{
    EventBus bus;
    int a_count = 0;
    int b_count = 0;

    auto s1 = bus.Subscribe<EventA>([&a_count](const EventA&) { ++a_count; });
    auto s2 = bus.Subscribe<EventB>([&b_count](const EventB&) { ++b_count; });

    bus.Publish(EventA{});
    bus.Publish(EventA{});
    bus.Publish(EventB{});

    Check(a_count == 2, "EventA subscriber fired twice");
    Check(b_count == 1, "EventB subscriber fired once");
}

// ================= Ordering =================

void TestSubscriptionOrderIsDeliveryOrder()
{
    EventBus bus;

    std::vector<int> order;

    auto s1 = bus.Subscribe<EventA>(
        [&order](const EventA&) { order.push_back(1); });
    auto s2 = bus.Subscribe<EventA>(
        [&order](const EventA&) { order.push_back(2); });
    auto s3 = bus.Subscribe<EventA>(
        [&order](const EventA&) { order.push_back(3); });

    bus.Publish(EventA{});

    Check(order == std::vector<int>({1, 2, 3}),
          "Subscription order equals delivery order");
}

void TestOrderPreservedAfterCancel()
{
    EventBus bus;

    std::vector<int> order;

    auto s1 = bus.Subscribe<EventA>(
        [&order](const EventA&) { order.push_back(1); });
    auto s2 = bus.Subscribe<EventA>(
        [&order](const EventA&) { order.push_back(2); });
    auto s3 = bus.Subscribe<EventA>(
        [&order](const EventA&) { order.push_back(3); });

    s2.Cancel();

    bus.Publish(EventA{});

    Check(order == std::vector<int>({1, 3}),
          "Order preserved after middle subscriber cancelled");
}

// ================= Subscription lifetime =================

void TestExplicitCancel()
{
    EventBus bus;
    int count = 0;

    auto sub = bus.Subscribe<EventA>(
        [&count](const EventA&) { ++count; });

    bus.Publish(EventA{});
    Check(count == 1, "Subscriber fires before Cancel");

    sub.Cancel();

    bus.Publish(EventA{});
    Check(count == 1, "Subscriber does not fire after Cancel");
    Check(!sub.IsValid(), "Cancel invalidates the Subscription handle");
}

void TestRAIICancel()
{
    EventBus bus;
    int count = 0;

    {
        auto sub = bus.Subscribe<EventA>(
            [&count](const EventA&) { ++count; });
        bus.Publish(EventA{});
    }   // sub destroyed here

    bus.Publish(EventA{});

    Check(count == 1,
          "RAII subscription removes itself on scope exit");
}

void TestMoveSubscription()
{
    EventBus bus;
    int count = 0;

    auto a = bus.Subscribe<EventA>(
        [&count](const EventA&) { ++count; });

    Subscription b = std::move(a);

    Check(!a.IsValid(), "Moved-from Subscription is invalid");
    Check(b.IsValid(), "Moved-to Subscription is valid");

    bus.Publish(EventA{});
    Check(count == 1, "Subscription still works after move");

    b.Cancel();
    bus.Publish(EventA{});
    Check(count == 1, "Cancel after move removes subscription");
}

void TestSubscriptionOutlivesBus()
{
    Subscription sub;
    {
        EventBus bus;
        sub = bus.Subscribe<EventA>([](const EventA&) {});
    }
    // bus destroyed; sub still holds weak_ptr
    sub.Cancel();   // must not crash
    Check(true, "Subscription::Cancel after bus destroyed is safe");
}

void TestDoubleCancel()
{
    EventBus bus;

    auto sub = bus.Subscribe<EventA>([](const EventA&) {});

    sub.Cancel();
    sub.Cancel();   // must be a no-op

    Check(true, "Double Cancel is safe");
}

// ================= Validation =================

void TestEmptyCallbackRejected()
{
    EventBus bus;

    auto sub = bus.Subscribe<EventA>({});

    Check(!sub.IsValid(),
          "Empty callback returns invalid Subscription");
    Check(bus.SubscriberCount() == 0,
          "Empty callback does not increase subscriber count");
}

void TestSubscriberLimit()
{
    EventBus bus;

    std::vector<Subscription> subs;
    subs.reserve(EventBus::kMaxSubscribers);

    for (std::size_t i = 0; i < EventBus::kMaxSubscribers; ++i)
    {
        subs.push_back(bus.Subscribe<EventA>([](const EventA&) {}));
    }

    Check(bus.SubscriberCount() == EventBus::kMaxSubscribers,
          "Subscriber count at limit");

    auto overflow = bus.Subscribe<EventA>([](const EventA&) {});
    Check(!overflow.IsValid(),
          "Subscription beyond limit returns invalid handle");
    Check(bus.SubscriberCount() == EventBus::kMaxSubscribers,
          "Subscriber count unchanged after overflow");
}

// ================= Reentrant publish =================

void TestReentrantPublishRejected()
{
    EventBus bus;

    EventError inner_result = EventError::SUCCESS;
    int outer_fires = 0;
    int b_fires = 0;

    auto a_sub = bus.Subscribe<EventA>(
        [&](const EventA&)
        {
            ++outer_fires;
            inner_result = bus.Publish(EventB{"nested"});
        });

    auto b_sub = bus.Subscribe<EventB>(
        [&b_fires](const EventB&) { ++b_fires; });

    bus.Publish(EventA{});

    Check(outer_fires == 1, "Outer subscriber fired");
    Check(inner_result == EventError::REENTRANT_PUBLISH,
          "Nested Publish returns REENTRANT_PUBLISH");
    Check(b_fires == 0, "Nested event not delivered");

    // After the outer dispatch completes, reentrancy guard is cleared.
    bus.Publish(EventB{"top"});
    Check(b_fires == 1, "Top-level publish works after reentrancy");
}

// ================= Exception handling =================

void TestSubscriberThrowDoesNotStopDelivery()
{
    EventBus bus;

    int a = 0, c = 0;

    auto s1 = bus.Subscribe<EventA>([&a](const EventA&) { ++a; });

    auto s2 = bus.Subscribe<EventA>([](const EventA&)
    {
        throw std::runtime_error("intentional test exception");
    });

    auto s3 = bus.Subscribe<EventA>([&c](const EventA&) { ++c; });

    auto r = bus.Publish(EventA{});

    Check(r == EventError::SUCCESS,
          "Publish succeeds even when a subscriber throws");
    Check(a == 1, "Subscriber before thrower still fired");
    Check(c == 1, "Subscriber after thrower still fired");
}

void TestThrowingSubscriberRetained()
{
    EventBus bus;

    int throws_attempted = 0;

    auto sub = bus.Subscribe<EventA>(
        [&throws_attempted](const EventA&)
        {
            ++throws_attempted;
            throw std::runtime_error("intentional");
        });

    bus.Publish(EventA{});
    bus.Publish(EventA{});
    bus.Publish(EventA{});

    Check(throws_attempted == 3,
          "Throwing subscriber is called on every publish");
}

// ================= Cancel during dispatch =================

void TestCancelDuringDispatchDoesNotCrash()
{
    EventBus bus;

    int a = 0, c = 0;
    Subscription c_sub;

    auto a_sub = bus.Subscribe<EventA>(
        [&](const EventA&)
        {
            ++a;
            c_sub.Cancel();   // cancel third subscriber mid-dispatch
        });

    auto b_sub = bus.Subscribe<EventA>([](const EventA&) {});

    c_sub = bus.Subscribe<EventA>([&c](const EventA&) { ++c; });

    bus.Publish(EventA{});

    Check(a == 1, "First subscriber fired");

    // Semantics: snapshot taken before dispatch. C still receives
    // the in-flight event, but not future ones.
    Check(c == 1, "Cancelled subscriber still received in-flight event");

    bus.Publish(EventA{});
    Check(c == 1, "Cancelled subscriber does not receive later events");
}

// ================= Concurrency =================

void TestConcurrentSubscribe()
{
    EventBus bus;

    constexpr int kThreads   = 4;
    constexpr int kPerThread = 8;

    std::vector<std::thread> threads;

    // Subscriptions must outlive the worker threads that created
    // them, otherwise their destructors cancel the subscription
    // before we can check the count. Collect them in a shared
    // vector under a mutex; the vector lives until end of test.
    std::mutex                subs_mutex;
    std::vector<Subscription> all_subs;
    all_subs.reserve(kThreads * kPerThread);

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&]()
        {
            std::vector<Subscription> local;
            local.reserve(kPerThread);

            for (int i = 0; i < kPerThread; ++i)
            {
                local.push_back(
                    bus.Subscribe<EventA>([](const EventA&) {}));
            }

            std::lock_guard<std::mutex> lock(subs_mutex);
            for (auto& s : local)
            {
                all_subs.push_back(std::move(s));
            }
        });
    }

    for (auto& th : threads) th.join();

    Check(bus.SubscriberCount() ==
          static_cast<std::size_t>(kThreads * kPerThread),
          "All concurrently-subscribed callbacks registered");

    // all_subs goes out of scope here — subscriptions release cleanly.
}

void TestConcurrentPublish()
{
    EventBus bus;

    Counter counter;

    auto sub = bus.Subscribe<EventA>(
        [&counter](const EventA&) { counter.Increment(); });

    constexpr int kThreads   = 4;
    constexpr int kPerThread = 25;

    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&bus]()
        {
            for (int i = 0; i < kPerThread; ++i)
            {
                bus.Publish(EventA{i});
            }
        });
    }

    for (auto& th : threads) th.join();

    Check(counter.Get() == kThreads * kPerThread,
          "Subscriber received every concurrently-published event");
}

void TestConcurrentSubscribeAndPublish()
{
    EventBus bus;

    Counter counter;
    std::atomic<bool> stop{false};

    // Long-lived subscriber that survives the whole test.
    auto sub = bus.Subscribe<EventA>(
        [&counter](const EventA&) { counter.Increment(); });

    // Publisher thread — keeps publishing while others subscribe.
    std::thread publisher([&]()
    {
        for (int i = 0; i < 500; ++i)
        {
            bus.Publish(EventA{i});
        }
        stop.store(true);
    });

    // Subscriber-mutation thread — subscribes and cancels repeatedly.
    std::thread mutator([&]()
    {
        while (!stop.load())
        {
            auto s = bus.Subscribe<EventA>([](const EventA&) {});
            s.Cancel();
        }
    });

    publisher.join();
    mutator.join();

    Check(counter.Get() == 500,
          "Long-lived subscriber received all 500 events during churn");
}

} // namespace

int main()
{
    std::fprintf(stderr, "=== EventBus Test Suite ===\n\n");

    TestPublishWithNoSubscribers();
    TestBasicSubscribeAndPublish();
    TestMultipleSubscribers();
    TestTypeIsolation();

    TestSubscriptionOrderIsDeliveryOrder();
    TestOrderPreservedAfterCancel();

    TestExplicitCancel();
    TestRAIICancel();
    TestMoveSubscription();
    TestSubscriptionOutlivesBus();
    TestDoubleCancel();

    TestEmptyCallbackRejected();
    TestSubscriberLimit();

    TestReentrantPublishRejected();

    TestSubscriberThrowDoesNotStopDelivery();
    TestThrowingSubscriberRetained();

    TestCancelDuringDispatchDoesNotCrash();

    TestConcurrentSubscribe();
    TestConcurrentPublish();
    TestConcurrentSubscribeAndPublish();

    std::fprintf(stderr, "\n=== Summary ===\n");
    std::fprintf(stderr, "Tests run:    %d\n", tests_run);
    std::fprintf(stderr, "Tests passed: %d\n", tests_passed);

    if (tests_run == tests_passed)
    {
        std::fprintf(stderr, "ALL EVENT BUS TESTS PASSED\n");
        return 0;
    }

    std::fprintf(stderr, "EVENT BUS TESTS FAILED\n");
    return 1;
}