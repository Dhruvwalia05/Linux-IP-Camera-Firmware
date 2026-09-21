#ifndef TIMER_SCHEDULER_H
#define TIMER_SCHEDULER_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

enum class TimerError
{
    SUCCESS,
    ALREADY_RUNNING,
    NOT_RUNNING,
    INVALID_ARGUMENT,
    TIMER_NOT_FOUND,
    TIMER_LIMIT_REACHED,
    THREAD_CREATION_FAILED,
    JOIN_FAILED
};

enum class TimerMode
{
    ONE_SHOT,
    PERIODIC
};

enum class PeriodMode
{
    FIXED_DELAY,   // next fire = previous callback return + period
    FIXED_RATE     // next fire = previous deadline + period
};

class TimerId
{
public:
    TimerId() = default;

    bool IsValid() const noexcept;

    bool operator==(const TimerId& other) const noexcept;
    bool operator!=(const TimerId& other) const noexcept;

private:
    friend class TimerScheduler;

    explicit TimerId(std::uint64_t value) noexcept;

    std::uint64_t value_ = 0;
};

class TimerScheduler
{
public:
    using Callback = std::function<void(TimerId)>;

    static constexpr std::size_t kMaxTimers = 64;

    TimerScheduler();
    ~TimerScheduler();

    TimerScheduler(const TimerScheduler&)            = delete;
    TimerScheduler& operator=(const TimerScheduler&) = delete;
    TimerScheduler(TimerScheduler&&)                 = delete;
    TimerScheduler& operator=(TimerScheduler&&)      = delete;

    TimerError Start();
    TimerError Stop();
    TimerError Join();

    bool IsRunning() const;

    TimerError ScheduleOneShot(std::chrono::milliseconds delay,
                               Callback callback,
                               TimerId& out_id);

    TimerError SchedulePeriodic(std::chrono::milliseconds period,
                                PeriodMode mode,
                                Callback callback,
                                TimerId& out_id);

    TimerError Cancel(TimerId id);

private:
    enum class TimerState
    {
        PENDING,
        RUNNING,
        CANCELLED
    };

    struct TimerEntry
    {
        TimerId                                 id;
        Callback                                callback;
        TimerMode                               mode;
        PeriodMode                              period_mode;
        std::chrono::milliseconds               period;
        std::chrono::steady_clock::time_point   next_fire;
        TimerState                              state;
    };

    void SchedulerLoop();

    TimerEntry* FindEntry(TimerId id);

    static std::chrono::steady_clock::time_point
    ComputeFirstFire(std::chrono::milliseconds delay);

    static std::chrono::steady_clock::time_point
    ComputeNextFire(const TimerEntry& entry);

    std::thread                 thread_;
    bool                        running_;
    bool                        reschedule_needed_;
    std::uint64_t               next_id_;

    std::vector<TimerEntry>     timers_;

    mutable std::mutex          mutex_;
    std::condition_variable     cv_;
};

#endif // TIMER_SCHEDULER_H