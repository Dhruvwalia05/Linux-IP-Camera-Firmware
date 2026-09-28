#include "framework/watchdog/watchdog.h"
#include "framework/logger/logger.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace {

int tests_run    = 0;
int tests_passed = 0;

// Tracking device that reports Close() through a caller-owned flag.
// Used by tests that need to observe the close event after the
// Watchdog (and therefore the device) has been destroyed.
class TrackingWatchdogDevice : public IWatchdogDevice
{
public:
    explicit TrackingWatchdogDevice(bool* closed_flag) noexcept
        : closed_flag_(closed_flag)
    {
    }

    bool Open(int) override { return true; }
    bool Kick() override { return true; }

    void Close() override
    {
        if (closed_flag_ != nullptr)
            *closed_flag_ = true;
    }

    bool IsOpen() const override
    {
        return closed_flag_ == nullptr || !*closed_flag_;
    }

private:
    bool* closed_flag_ = nullptr;
};

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

// ================= NullWatchdogDevice behavior =================

void TestNullDeviceInitialState()
{
    NullWatchdogDevice dev;
    Check(!dev.IsOpen(),        "null dev: initially closed");
    Check(dev.KickCount() == 0, "null dev: no kicks yet");
    Check(!dev.WasClosed(),     "null dev: not yet closed");
}

void TestNullDeviceOpenClose()
{
    NullWatchdogDevice dev;
    Check(dev.Open(5),          "null dev: open succeeds");
    Check(dev.IsOpen(),         "null dev: open state");
    Check(dev.TimeoutSeconds() == 5, "null dev: timeout recorded");
    dev.Close();
    Check(dev.WasClosed(),      "null dev: close recorded");
    Check(!dev.IsOpen(),        "null dev: closed state");
}

void TestNullDeviceRejectsInvalidTimeout()
{
    NullWatchdogDevice dev;
    Check(!dev.Open(0),  "null dev: zero timeout rejected");
    Check(!dev.Open(-1), "null dev: negative timeout rejected");
}

void TestNullDeviceKickCounting()
{
    NullWatchdogDevice dev;
    dev.Open(5);
    Check(dev.Kick(), "null dev: kick 1");
    Check(dev.Kick(), "null dev: kick 2");
    Check(dev.Kick(), "null dev: kick 3");
    Check(dev.KickCount() == 3, "null dev: kick count is 3");
}

// ================= Watchdog lifecycle =================

void TestDefaultConstructor()
{
    Watchdog wd;
    Check(!wd.IsRunning(),           "watchdog: not running initially");
    Check(wd.CheckerCount() == 0,    "watchdog: no checkers initially");
}

void TestRegisterChecker()
{
    Watchdog wd;
    Check(wd.RegisterChecker("camera", [] { return true; }),
          "watchdog: register camera");
    Check(wd.CheckerCount() == 1, "watchdog: count = 1");

    Check(wd.RegisterChecker("network", [] { return true; }),
          "watchdog: register network");
    Check(wd.CheckerCount() == 2, "watchdog: count = 2");
}

void TestRegisterDuplicateNameRejected()
{
    Watchdog wd;
    wd.RegisterChecker("camera", [] { return true; });

    Check(!wd.RegisterChecker("camera", [] { return true; }),
          "watchdog: duplicate name rejected");
    Check(wd.CheckerCount() == 1, "watchdog: count unchanged");
}

void TestRegisterEmptyNameOrEmptyFn()
{
    Watchdog wd;
    Check(!wd.RegisterChecker("", [] { return true; }),
          "watchdog: empty name rejected");
    Check(!wd.RegisterChecker("x", {}),
          "watchdog: empty checker rejected");
    Check(wd.CheckerCount() == 0, "watchdog: nothing registered");
}

void TestStartWithoutCheckersFails()
{
    Watchdog wd;
    Check(!wd.Start(5), "watchdog: Start with no checkers fails");
    Check(!wd.IsRunning(), "watchdog: still not running");
}

void TestStartWithInvalidTimeoutFails()
{
    Watchdog wd;
    wd.RegisterChecker("x", [] { return true; });
    Check(!wd.Start(0),  "watchdog: zero timeout fails");
    Check(!wd.Start(-1), "watchdog: negative timeout fails");
}

void TestStartAndStopLifecycle()
{
    auto* dev_ptr = new NullWatchdogDevice();
    std::unique_ptr<IWatchdogDevice> dev(dev_ptr);

    Watchdog wd(std::move(dev));
    wd.RegisterChecker("camera", [] { return true; });

    Check(wd.Start(5), "watchdog: Start succeeds");
    Check(wd.IsRunning(), "watchdog: IsRunning true");
    Check(dev_ptr->IsOpen(), "watchdog: device opened");
    Check(dev_ptr->TimeoutSeconds() == 5,
          "watchdog: device timeout set");

    wd.Stop();
    wd.Join();

    Check(!wd.IsRunning(), "watchdog: not running after Join");
    Check(dev_ptr->WasClosed(), "watchdog: device closed");
}

void TestDoubleStartRejected()
{
    auto* dev_ptr = new NullWatchdogDevice();
    std::unique_ptr<IWatchdogDevice> dev(dev_ptr);

    Watchdog wd(std::move(dev));
    wd.RegisterChecker("x", [] { return true; });

    Check(wd.Start(5), "watchdog: first start");
    Check(!wd.Start(5), "watchdog: double start rejected");

    wd.Stop();
    wd.Join();
}

void TestRegisterAfterStartRejected()
{
    auto* dev_ptr = new NullWatchdogDevice();
    std::unique_ptr<IWatchdogDevice> dev(dev_ptr);

    Watchdog wd(std::move(dev));
    wd.RegisterChecker("x", [] { return true; });
    wd.Start(5);

    Check(!wd.RegisterChecker("y", [] { return true; }),
          "watchdog: register after Start rejected");

    wd.Stop();
    wd.Join();
}

void TestDoubleJoinSafe()
{
    auto* dev_ptr = new NullWatchdogDevice();
    std::unique_ptr<IWatchdogDevice> dev(dev_ptr);

    Watchdog wd(std::move(dev));
    wd.RegisterChecker("x", [] { return true; });
    wd.Start(5);

    wd.Join();
    wd.Join();

    Check(true, "watchdog: double Join is safe");
}

void TestDestructorCleansUp()
{
    // The flag lives on the test's stack, so it survives the
    // Watchdog (and the device it owns) going out of scope.
    bool was_closed = false;

    {
        Watchdog wd(std::make_unique<TrackingWatchdogDevice>(&was_closed));
        wd.RegisterChecker("x", [] { return true; });
        wd.Start(5);
        // intentional: no explicit Stop / Join
    }

    Check(was_closed, "watchdog: destructor closed device");
}

// ================= Health-aware kicking =================

void TestHealthyCheckersCauseKicks()
{
    auto* dev_ptr = new NullWatchdogDevice();
    std::unique_ptr<IWatchdogDevice> dev(dev_ptr);

    Watchdog wd(std::move(dev));
    wd.RegisterChecker("camera", [] { return true; });
    wd.RegisterChecker("network", [] { return true; });

    wd.Start(2);   // timeout 2 s → kick interval 1 s

    std::this_thread::sleep_for(2500ms);

    const int kicks = dev_ptr->KickCount();
    Check(kicks >= 2, "watchdog: healthy checkers cause kicks");

    wd.Stop();
    wd.Join();
}

void TestUnhealthyCheckerStopsKicks()
{
    auto* dev_ptr = new NullWatchdogDevice();
    std::unique_ptr<IWatchdogDevice> dev(dev_ptr);

    Watchdog wd(std::move(dev));

    std::atomic<bool> healthy{true};
    wd.RegisterChecker("camera",
        [&healthy] { return healthy.load(); });

    wd.Start(2);

    std::this_thread::sleep_for(1500ms);
    const int kicks_before = dev_ptr->KickCount();
    Check(kicks_before >= 1,
          "watchdog: kicks while healthy");

    healthy.store(false);

    std::this_thread::sleep_for(2500ms);
    const int kicks_after = dev_ptr->KickCount();

    Check(kicks_after == kicks_before,
          "watchdog: no kicks after unhealthy");

    wd.Stop();
    wd.Join();
}

void TestRecoveryResumesKicks()
{
    auto* dev_ptr = new NullWatchdogDevice();
    std::unique_ptr<IWatchdogDevice> dev(dev_ptr);

    Watchdog wd(std::move(dev));

    std::atomic<bool> healthy{true};
    wd.RegisterChecker("camera",
        [&healthy] { return healthy.load(); });

    wd.Start(2);

    std::this_thread::sleep_for(1500ms);
    const int kicks_a = dev_ptr->KickCount();

    healthy.store(false);
    std::this_thread::sleep_for(2500ms);
    const int kicks_b = dev_ptr->KickCount();

    healthy.store(true);
    std::this_thread::sleep_for(2500ms);
    const int kicks_c = dev_ptr->KickCount();

    Check(kicks_a >= 1,         "watchdog: kicks in healthy phase A");
    Check(kicks_b == kicks_a,   "watchdog: no kicks in unhealthy phase");
    Check(kicks_c > kicks_b,    "watchdog: kicks resume after recovery");

    wd.Stop();
    wd.Join();
}

void TestThrowingCheckerStopsKicks()
{
    auto* dev_ptr = new NullWatchdogDevice();
    std::unique_ptr<IWatchdogDevice> dev(dev_ptr);

    Watchdog wd(std::move(dev));

    std::atomic<bool> should_throw{false};
    wd.RegisterChecker("camera",
        [&should_throw]() -> bool
        {
            if (should_throw.load())
                throw std::runtime_error("simulated failure");
            return true;
        });

    wd.Start(2);

    std::this_thread::sleep_for(1500ms);
    const int kicks_before = dev_ptr->KickCount();
    Check(kicks_before >= 1,
          "watchdog: kicking before checker throws");

    should_throw.store(true);

    std::this_thread::sleep_for(2500ms);
    const int kicks_after = dev_ptr->KickCount();

    Check(kicks_after == kicks_before,
          "watchdog: throwing checker stops kicks");

    wd.Stop();
    wd.Join();
}

void TestAllCheckersAreEvaluated()
{
    auto* dev_ptr = new NullWatchdogDevice();
    std::unique_ptr<IWatchdogDevice> dev(dev_ptr);

    Watchdog wd(std::move(dev));

    std::atomic<int> eval_count{0};

    wd.RegisterChecker("a", [&eval_count]() { ++eval_count; return true; });
    wd.RegisterChecker("b", [&eval_count]() { ++eval_count; return true; });
    wd.RegisterChecker("c", [&eval_count]() { ++eval_count; return true; });

    wd.Start(2);
    std::this_thread::sleep_for(1500ms);
    wd.Stop();
    wd.Join();

    // Each kick triggers evaluation of all three checkers.
    // Over 1.5s with 1s interval: one kick → at least 3 evaluations.
    Check(eval_count.load() >= 3,
          "watchdog: all checkers evaluated on healthy path");
}

} // namespace

int main()
{
    std::fprintf(stderr, "=== Watchdog Test Suite ===\n\n");

    Logger::GetInstance().Initialize();

    TestNullDeviceInitialState();
    TestNullDeviceOpenClose();
    TestNullDeviceRejectsInvalidTimeout();
    TestNullDeviceKickCounting();

    TestDefaultConstructor();
    TestRegisterChecker();
    TestRegisterDuplicateNameRejected();
    TestRegisterEmptyNameOrEmptyFn();
    TestStartWithoutCheckersFails();
    TestStartWithInvalidTimeoutFails();
    TestStartAndStopLifecycle();
    TestDoubleStartRejected();
    TestRegisterAfterStartRejected();
    TestDoubleJoinSafe();
    TestDestructorCleansUp();

    TestHealthyCheckersCauseKicks();
    TestUnhealthyCheckerStopsKicks();
    TestRecoveryResumesKicks();
    TestThrowingCheckerStopsKicks();
    TestAllCheckersAreEvaluated();

    Logger::GetInstance().Shutdown();

    std::fprintf(stderr, "\n=== Summary ===\n");
    std::fprintf(stderr, "Tests run:    %d\n", tests_run);
    std::fprintf(stderr, "Tests passed: %d\n", tests_passed);

    if (tests_run == tests_passed)
    {
        std::fprintf(stderr, "ALL WATCHDOG TESTS PASSED\n");
        return 0;
    }

    std::fprintf(stderr, "WATCHDOG TESTS FAILED\n");
    return 1;
}