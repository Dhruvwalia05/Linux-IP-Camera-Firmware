#include "framework/timer/timer_scheduler.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
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

template <typename Pred>
bool WaitFor(Pred pred, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

// Mutex-guarded counter + condition variable. Helgrind-friendly —
// no annotations needed because every access is under the lock.
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

// ================= Lifecycle =================

void TestInitialState()
{
    TimerScheduler sched;
    Check(!sched.IsRunning(), "Initial state: not running");
}

void TestStartStopJoin()
{
    TimerScheduler sched;

    Check(sched.Start() == TimerError::SUCCESS, "Start succeeds");
    Check(sched.IsRunning(), "IsRunning true after Start");
    Check(sched.Stop() == TimerError::SUCCESS, "Stop succeeds");
    Check(sched.Join() == TimerError::SUCCESS, "Join succeeds");
    Check(!sched.IsRunning(), "IsRunning false after Join");
}

void TestDoubleStart()
{
    TimerScheduler sched;
    sched.Start();

    Check(sched.Start() == TimerError::ALREADY_RUNNING,
          "Double Start rejected");

    sched.Stop();
    sched.Join();
}

void TestStopBeforeStart()
{
    TimerScheduler sched;
    Check(sched.Stop() == TimerError::NOT_RUNNING,
          "Stop before Start = NOT_RUNNING");
}

void TestJoinBeforeStart()
{
    TimerScheduler sched;
    Check(sched.Join() == TimerError::NOT_RUNNING,
          "Join before Start = NOT_RUNNING");
}

void TestRestartAfterJoin()
{
    TimerScheduler sched;

    sched.Start();
    sched.Stop();
    sched.Join();

    Check(sched.Start() == TimerError::SUCCESS,
          "Restart after Join succeeds");
    Check(sched.IsRunning(), "IsRunning true after restart");

    sched.Stop();
    sched.Join();
}

// ================= Validation =================

void TestZeroDelayRejected()
{
    TimerScheduler sched;
    sched.Start();

    TimerId id;
    auto r = sched.ScheduleOneShot(0ms, [](TimerId) {}, id);

    Check(r == TimerError::INVALID_ARGUMENT,
          "Zero delay rejected");
    Check(!id.IsValid(), "out_id untouched on rejection");

    sched.Stop();
    sched.Join();
}

void TestEmptyCallbackRejected()
{
    TimerScheduler sched;
    sched.Start();

    TimerId id;
    auto r = sched.ScheduleOneShot(50ms, {}, id);

    Check(r == TimerError::INVALID_ARGUMENT,
          "Empty callback rejected");

    sched.Stop();
    sched.Join();
}

void TestScheduleOnStopped()
{
    TimerScheduler sched;

    TimerId id;
    auto r = sched.ScheduleOneShot(50ms, [](TimerId) {}, id);

    Check(r == TimerError::NOT_RUNNING,
          "Schedule on stopped scheduler = NOT_RUNNING");
}

// ================= One-shot =================

void TestOneShotFiresOnce()
{
    TimerScheduler sched;
    sched.Start();

    Counter counter;
    TimerId id;

    auto r = sched.ScheduleOneShot(30ms,
        [&counter](TimerId) { counter.Increment(); }, id);

    Check(r == TimerError::SUCCESS, "One-shot Schedule succeeds");
    Check(id.IsValid(), "One-shot TimerId is valid");

    Check(counter.WaitForAtLeast(1, 500ms),
          "One-shot fires within timeout");

    std::this_thread::sleep_for(100ms);
    Check(counter.Get() == 1, "One-shot fires exactly once");

    Check(sched.Cancel(id) == TimerError::TIMER_NOT_FOUND,
          "Cancel after fire = TIMER_NOT_FOUND");

    sched.Stop();
    sched.Join();
}

// ================= Periodic =================

void TestPeriodicFiresRepeatedly()
{
    TimerScheduler sched;
    sched.Start();

    Counter counter;
    TimerId id;

    auto r = sched.SchedulePeriodic(30ms, PeriodMode::FIXED_DELAY,
        [&counter](TimerId) { counter.Increment(); }, id);

    Check(r == TimerError::SUCCESS, "Periodic Schedule succeeds");

    Check(counter.WaitForAtLeast(3, 500ms),
          "Periodic fires at least 3 times");

    Check(sched.Cancel(id) == TimerError::SUCCESS,
          "Cancel periodic succeeds");

    const int stopped_at = counter.Get();
    std::this_thread::sleep_for(150ms);
    Check(counter.Get() == stopped_at,
          "Periodic stops firing after Cancel");

    sched.Stop();
    sched.Join();
}

void TestSelfCancelInsideCallback()
{
    TimerScheduler sched;
    sched.Start();

    Counter counter;
    TimerId id;

    sched.SchedulePeriodic(20ms, PeriodMode::FIXED_DELAY,
        [&counter, &sched](TimerId self_id)
        {
            counter.Increment();
            if (counter.Get() >= 3)
            {
                sched.Cancel(self_id);
            }
        }, id);

    Check(counter.WaitForAtLeast(3, 500ms),
          "Self-cancel: 3rd fire reached");

    std::this_thread::sleep_for(150ms);
    Check(counter.Get() == 3,
          "Self-cancel: no further fires after 3rd");

    sched.Stop();
    sched.Join();
}

void TestFixedRateNoBurst()
{
    TimerScheduler sched;
    sched.Start();

    std::mutex m;
    std::vector<std::chrono::steady_clock::time_point> fire_starts;

    TimerId id;
    sched.SchedulePeriodic(50ms, PeriodMode::FIXED_RATE,
        [&](TimerId)
        {
            {
                std::lock_guard<std::mutex> lock(m);
                fire_starts.push_back(std::chrono::steady_clock::now());
            }
            std::this_thread::sleep_for(100ms);
        }, id);

    WaitFor([&] {
        std::lock_guard<std::mutex> lock(m);
        return fire_starts.size() >= 3;
    }, 3000ms);

    sched.Stop();
    sched.Join();

    std::lock_guard<std::mutex> lock(m);

    bool no_burst = fire_starts.size() >= 3;

    for (std::size_t i = 1; i < fire_starts.size(); ++i)
    {
        const auto delta = fire_starts[i] - fire_starts[i - 1];

        // Callback duration is 100ms, period is 50ms.
        // Without the catch-up cap: next fire = callback return -> delta ~= 100ms.
        // With the cap: next fire = callback return + 50ms -> delta ~= 150ms.
        // Threshold 125ms distinguishes the two.
        if (delta < 125ms)
        {
            no_burst = false;
            break;
        }
    }

    Check(no_burst, "Fixed-rate: no back-to-back catch-up burst");
}

// ================= Cancel =================

void TestCancelPending()
{
    TimerScheduler sched;
    sched.Start();

    Counter counter;
    TimerId id;

    sched.ScheduleOneShot(500ms,
        [&counter](TimerId) { counter.Increment(); }, id);

    Check(sched.Cancel(id) == TimerError::SUCCESS,
          "Cancel pending timer succeeds");

    std::this_thread::sleep_for(200ms);
    Check(counter.Get() == 0,
          "Cancelled timer never fires");

    Check(sched.Cancel(id) == TimerError::TIMER_NOT_FOUND,
          "Re-cancel returns TIMER_NOT_FOUND");

    sched.Stop();
    sched.Join();
}

void TestCancelInvalidId()
{
    TimerScheduler sched;
    sched.Start();

    TimerId invalid;
    Check(sched.Cancel(invalid) == TimerError::INVALID_ARGUMENT,
          "Cancel invalid TimerId = INVALID_ARGUMENT");

    sched.Stop();
    sched.Join();
}

// ================= Exception handling =================

void TestCallbackThrowsCancelsPeriodic()
{
    TimerScheduler sched;
    sched.Start();

    Counter counter;
    TimerId id;

    sched.SchedulePeriodic(20ms, PeriodMode::FIXED_DELAY,
        [&counter](TimerId)
        {
            int n = 0;
            {
                std::lock_guard<std::mutex> lock(counter.m);
                n = ++counter.value;
                counter.cv.notify_all();
            }
            if (n == 2)
            {
                throw std::runtime_error("intentional test exception");
            }
        }, id);

    Check(counter.WaitForAtLeast(2, 500ms),
          "Periodic fired twice");

    std::this_thread::sleep_for(150ms);
    Check(counter.Get() == 2,
          "Callback throw cancels the periodic timer");

    // Scheduler must remain alive and useful.
    Counter after;
    TimerId id2;
    auto r = sched.ScheduleOneShot(20ms,
        [&after](TimerId) { after.Increment(); }, id2);

    Check(r == TimerError::SUCCESS,
          "Scheduler accepts new timer after callback throw");
    Check(after.WaitForAtLeast(1, 500ms),
          "Scheduler continues running after callback throw");

    sched.Stop();
    sched.Join();
}

// ================= Limits =================

void TestTimerLimitReached()
{
    TimerScheduler sched;
    sched.Start();

    std::size_t scheduled = 0;

    for (std::size_t i = 0; i < TimerScheduler::kMaxTimers; ++i)
    {
        TimerId id;
        auto r = sched.ScheduleOneShot(10s, [](TimerId) {}, id);
        if (r != TimerError::SUCCESS) break;
        ++scheduled;
    }

    Check(scheduled == TimerScheduler::kMaxTimers,
          "Scheduled up to kMaxTimers");

    TimerId extra;
    auto r = sched.ScheduleOneShot(10s, [](TimerId) {}, extra);

    Check(r == TimerError::TIMER_LIMIT_REACHED,
          "Schedule beyond limit = TIMER_LIMIT_REACHED");

    sched.Stop();
    sched.Join();
}

// ================= Concurrency =================

void TestConcurrentSchedule()
{
    TimerScheduler sched;
    sched.Start();

    Counter counter;

    constexpr int kThreads   = 4;
    constexpr int kPerThread = 4;

    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&sched, &counter]()
        {
            for (int i = 0; i < kPerThread; ++i)
            {
                TimerId id;
                sched.ScheduleOneShot(20ms,
                    [&counter](TimerId) { counter.Increment(); },
                    id);
            }
        });
    }

    for (auto& th : threads) th.join();

    Check(counter.WaitForAtLeast(kThreads * kPerThread, 1000ms),
          "All concurrently-scheduled timers fired");

    sched.Stop();
    sched.Join();
}

void TestConcurrentCancel()
{
    TimerScheduler sched;
    sched.Start();

    Counter counter;

    std::vector<TimerId> ids;
    constexpr int kTimers = 20;

    for (int i = 0; i < kTimers; ++i)
    {
        TimerId id;
        sched.ScheduleOneShot(500ms,
            [&counter](TimerId) { counter.Increment(); }, id);
        ids.push_back(id);
    }

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&sched, &ids, t]()
        {
            for (std::size_t i = t; i < ids.size(); i += 4)
            {
                sched.Cancel(ids[i]);
            }
        });
    }

    for (auto& th : threads) th.join();

    std::this_thread::sleep_for(300ms);
    Check(counter.Get() == 0,
          "All concurrently-cancelled timers never fired");

    sched.Stop();
    sched.Join();
}

} // namespace

int main()
{
    std::fprintf(stderr, "=== TimerScheduler Test Suite ===\n\n");

    TestInitialState();
    TestStartStopJoin();
    TestDoubleStart();
    TestStopBeforeStart();
    TestJoinBeforeStart();
    TestRestartAfterJoin();

    TestZeroDelayRejected();
    TestEmptyCallbackRejected();
    TestScheduleOnStopped();

    TestOneShotFiresOnce();

    TestPeriodicFiresRepeatedly();
    TestSelfCancelInsideCallback();
    TestFixedRateNoBurst();

    TestCancelPending();
    TestCancelInvalidId();

    TestCallbackThrowsCancelsPeriodic();

    TestTimerLimitReached();

    TestConcurrentSchedule();
    TestConcurrentCancel();

    std::fprintf(stderr, "\n=== Summary ===\n");
    std::fprintf(stderr, "Tests run:    %d\n", tests_run);
    std::fprintf(stderr, "Tests passed: %d\n", tests_passed);

    if (tests_run == tests_passed)
    {
        std::fprintf(stderr, "ALL TIMER SCHEDULER TESTS PASSED\n");
        return 0;
    }

    std::fprintf(stderr, "TIMER SCHEDULER TESTS FAILED\n");
    return 1;
}