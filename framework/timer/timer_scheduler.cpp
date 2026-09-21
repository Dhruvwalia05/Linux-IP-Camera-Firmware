#include "timer_scheduler.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <optional>
#include <system_error>
#include <utility>

namespace
{

struct ExecutingTimer
{
    TimerId                     id;
    std::function<void(TimerId)> callback;
    TimerMode                   mode;
    std::chrono::milliseconds   period;
    PeriodMode                  period_mode;
    bool                        callback_threw = false;
};

void ExecuteCallback(ExecutingTimer& task)
{
    try
    {
        task.callback(task.id);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr,
                     "[TimerScheduler] callback threw std::exception: %s\n",
                     e.what());
        task.callback_threw = true;
    }
    catch (...)
    {
        std::fprintf(stderr,
                     "[TimerScheduler] callback threw unknown exception\n");
        task.callback_threw = true;
    }
}

} // namespace

TimerScheduler::TimerScheduler()
    :   running_(false),
        reschedule_needed_(false),
        next_id_(1)
{
}

TimerScheduler::~TimerScheduler()
{
    if (thread_.joinable())
    {
        std::fprintf(
            stderr,
            "[TimerScheduler] FATAL: destructor called while scheduler "
            "thread is still joinable. Caller must call Stop() and Join() "
            "before destroying TimerScheduler. Terminating.\n");

        std::terminate();
    }
}

TimerId::TimerId(std::uint64_t value) noexcept
    : value_(value)
{
}

bool TimerId::IsValid() const noexcept
{
    return value_ != 0;
}

bool TimerId::operator==(const TimerId& other) const noexcept
{
    return value_ == other.value_;
}

bool TimerId::operator!=(const TimerId& other) const noexcept
{
    return !(*this == other);
}

TimerError TimerScheduler::Start()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if(running_)
    {
        return TimerError::ALREADY_RUNNING;
    }

    try
    {
        thread_ = std::thread(&TimerScheduler::SchedulerLoop, this);
        running_ = true;
    }
    catch (std::system_error&)
    {
        running_ = false;
        return TimerError::THREAD_CREATION_FAILED;
    }

    return TimerError::SUCCESS;
}

TimerError TimerScheduler::Stop()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if(!running_)
    {
        return TimerError::NOT_RUNNING;
    }
    running_ = false;
    cv_.notify_all();

    return TimerError::SUCCESS;
}

TimerError TimerScheduler::Join()
{
    std::thread to_join;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if(!thread_.joinable())
        {
            return TimerError::NOT_RUNNING;
        }
        to_join = std::move(thread_);
    }

    to_join.join();

    return TimerError::SUCCESS;
}

bool TimerScheduler::IsRunning() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return running_;
}

TimerError TimerScheduler::ScheduleOneShot(std::chrono::milliseconds delay,
                               Callback callback,
                               TimerId& out_id)
{
    if(delay <= std::chrono::milliseconds (0) || !callback)
    {
        return TimerError::INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if(!running_)
    {
        return TimerError::NOT_RUNNING;
    }

    if(timers_.size() >= kMaxTimers)
    {
        return TimerError::TIMER_LIMIT_REACHED;
    }

    const TimerId id(next_id_++);
    TimerEntry entry;
    entry.id          = id;
    entry.callback    = std::move(callback);
    entry.mode        = TimerMode::ONE_SHOT;
    entry.period_mode = PeriodMode::FIXED_DELAY;
    entry.period      = std::chrono::milliseconds(0);
    entry.next_fire   = std::chrono::steady_clock::now() + delay;
    entry.state       = TimerState::PENDING;

    timers_.push_back(std::move(entry));

    out_id = id;

    reschedule_needed_ = true;
    cv_.notify_all();

    return TimerError::SUCCESS;
}

TimerError TimerScheduler::SchedulePeriodic(std::chrono::milliseconds period,
                                PeriodMode mode,
                                Callback callback,
                                TimerId& out_id)
{
    if(period <= std::chrono::milliseconds (0) || !callback)
    {
        return TimerError::INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if(!running_)
    {
        return TimerError::NOT_RUNNING;
    }

    if(timers_.size() >= kMaxTimers)
    {
        return TimerError::TIMER_LIMIT_REACHED;
    }

    const TimerId id(next_id_++);
    TimerEntry entry;
    entry.id          = id;
    entry.callback    = std::move(callback);
    entry.mode        = TimerMode::PERIODIC;
    entry.period_mode = mode;
    entry.period      = period;
    entry.next_fire   = std::chrono::steady_clock::now() + period;
    entry.state       = TimerState::PENDING;

    timers_.push_back(std::move(entry));
    out_id = id;

    reschedule_needed_ = true;
    cv_.notify_all();

    return TimerError::SUCCESS;
}

TimerError TimerScheduler::Cancel(TimerId id)
{
    if(!id.IsValid())
    {
        return TimerError::INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    TimerEntry* entry = FindEntry(id);

    if(entry == nullptr)
    {
        return TimerError::TIMER_NOT_FOUND;
    }

    switch (entry->state)
    {
        case TimerState::PENDING :
        {
            timers_.erase(
                std::remove_if(
                    timers_.begin(),
                    timers_.end(),
                    [id](const TimerEntry& e) { return e.id == id; }
                ),
                timers_.end()
            );

            reschedule_needed_ = true;
            cv_.notify_all();

            return TimerError::SUCCESS;
        }

        case TimerState::RUNNING :
        {
            entry->state  = TimerState::CANCELLED;
            return TimerError::SUCCESS;
        }

        case TimerState::CANCELLED :
        {
            return TimerError::TIMER_NOT_FOUND;
        }
    }

    return TimerError::TIMER_NOT_FOUND;
}

TimerScheduler::TimerEntry* TimerScheduler::FindEntry(TimerId id)
{
    for (TimerEntry& entry : timers_)
    {
        if (entry.id == id)
        {
            return &entry;
        }
    }

    return nullptr;
}

void TimerScheduler::SchedulerLoop()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!running_)
        {
            return;
        }

        // ---- Phase 1: find earliest pending deadline ----
        std::optional<std::chrono::steady_clock::time_point> next_deadline;

        for (const TimerEntry& entry : timers_)
        {
            if (entry.state != TimerState::PENDING)
            {
                continue;
            }

            if (!next_deadline.has_value() ||
                entry.next_fire < *next_deadline)
            {
                next_deadline = entry.next_fire;
            }
        }

        if (!next_deadline.has_value())
        {
            // No timers pending. Wait for a new schedule or stop.
            cv_.wait(lock, [this]()
            {
                return !running_ || reschedule_needed_;
            });
            reschedule_needed_ = false;
            continue;
        }

        // ---- Phase 2: sleep until the deadline OR a reschedule ----
        cv_.wait_until(lock, *next_deadline, [this]()
        {
            return !running_ || reschedule_needed_;
        });
        reschedule_needed_ = false;

        if (!running_)
        {
            return;
        }

        // ---- Phase 3: collect due timers ----
        const auto now = std::chrono::steady_clock::now();
        std::vector<ExecutingTimer> to_run;

        for (TimerEntry& entry : timers_)
        {
            if (entry.state != TimerState::PENDING)
            {
                continue;
            }

            if (entry.next_fire > now)
            {
                continue;
            }

            entry.state = TimerState::RUNNING;

            ExecutingTimer task;
            task.id             = entry.id;
            task.callback       = entry.callback;   // copy
            task.mode           = entry.mode;
            task.period         = entry.period;
            task.period_mode    = entry.period_mode;
            task.callback_threw = false;

            to_run.push_back(std::move(task));
        }

        lock.unlock();

        // ---- Phase 4: execute callbacks without the lock ----
        for (ExecutingTimer& task : to_run)
        {
            ExecuteCallback(task);
        }

        // ---- Phase 5: reconcile state ----
        lock.lock();

        std::vector<TimerId> to_erase;

        for (const ExecutingTimer& task : to_run)
        {
            TimerEntry* entry = FindEntry(task.id);
            if (entry == nullptr)
            {
                continue;
            }

            if (entry->state == TimerState::CANCELLED)
            {
                to_erase.push_back(task.id);
                continue;
            }

            if (task.callback_threw)
            {
                std::fprintf(
                    stderr,
                    "[TimerScheduler] cancelling timer after callback "
                    "threw an exception\n");
                to_erase.push_back(task.id);
                continue;
            }

            if (task.mode == TimerMode::ONE_SHOT)
            {
                to_erase.push_back(task.id);
                continue;
            }

            // ---- PERIODIC: re-arm ----
            if (task.period_mode == PeriodMode::FIXED_DELAY)
            {
                entry->next_fire =
                    std::chrono::steady_clock::now() + task.period;
            }
            else // FIXED_RATE
            {
                const auto now_after =
                    std::chrono::steady_clock::now();

                auto candidate = entry->next_fire + task.period;

                // Cap catch-up: if we're already past the next
                // deadline, schedule one period from now instead of
                // firing back-to-back.
                if (candidate <= now_after)
                {
                    candidate = now_after + task.period;
                }

                entry->next_fire = candidate;
            }

            entry->state = TimerState::PENDING;
        }

        if (!to_erase.empty())
        {
            timers_.erase(
                std::remove_if(
                    timers_.begin(),
                    timers_.end(),
                    [&to_erase](const TimerEntry& e)
                    {
                        for (TimerId id : to_erase)
                        {
                            if (e.id == id)
                            {
                                return true;
                            }
                        }
                        return false;
                    }),
                timers_.end());
        }
    }
}
