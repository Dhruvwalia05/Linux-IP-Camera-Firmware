#include "thread_manager.h"

#include <pthread.h>

#include <cstdio>
#include <exception>
#include <system_error>

namespace
{

constexpr std::size_t MAX_THREAD_NAME_LENGTH = 15;

std::string NormalizeName(const std::string& raw)
{
    if (raw.empty())
        return "worker";

    if (raw.size() <= MAX_THREAD_NAME_LENGTH)
        return raw;

    return raw.substr(0, MAX_THREAD_NAME_LENGTH);
}

} // namespace

ThreadManager::ThreadManager()
    : running(false),
      worker_finished(false)
{
}

ThreadManager::~ThreadManager()
{
    if (worker_thread.joinable())
    {
        std::fprintf(
            stderr,
            "[ThreadManager] FATAL: destructor called while worker "
            "'%s' is still running. Caller must call Stop() and Join() "
            "before destroying ThreadManager. Terminating.\n",
            name.c_str());

        std::terminate();
    }
}

ThreadError ThreadManager::Start(std::function<void(StopToken&)> worker)
{
    return Start("worker", std::move(worker));
}

ThreadError ThreadManager::Start(const std::string& thread_name,
                                 std::function<void(StopToken&)> worker)
{
    if (!worker)
        return ThreadError::INVALID_ARGUMENT;

    std::lock_guard<std::mutex> lock(mutex);

    if (running)
        return ThreadError::ALREADY_RUNNING;

    stop_token.Reset();
    worker_finished = false;
    name = NormalizeName(thread_name);

    const std::string thread_name_copy = name;

    try
    {
        worker_thread = std::thread(
            [this, worker = std::move(worker), thread_name_copy]()
            {
                pthread_setname_np(pthread_self(),
                                   thread_name_copy.c_str());

                worker(stop_token);

                {
                    std::lock_guard<std::mutex> inner_lock(mutex);
                    worker_finished = true;
                    completion_cv.notify_all();
                }
            });

        running = true;
    }
    catch (const std::system_error&)
    {
        running = false;
        return ThreadError::THREAD_CREATION_FAILED;
    }

    return ThreadError::SUCCESS;
}

ThreadError ThreadManager::Stop()
{
    std::lock_guard<std::mutex> lock(mutex);

    if (!running)
        return ThreadError::NOT_RUNNING;

    stop_token.RequestStop();

    return ThreadError::SUCCESS;
}

ThreadError ThreadManager::Join()
{
    std::unique_lock<std::mutex> lock(mutex);

    if (!running)
        return ThreadError::NOT_RUNNING;

    completion_cv.wait(lock,
        [this]() { return worker_finished; });

    return CompleteJoin(lock);
}

ThreadError ThreadManager::Join(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex);

    if (!running)
        return ThreadError::NOT_RUNNING;

    if (!completion_cv.wait_for(lock, timeout,
            [this]() { return worker_finished; }))
    {
        return ThreadError::JOIN_TIMEOUT;
    }

    return CompleteJoin(lock);
}

ThreadError ThreadManager::CompleteJoin(std::unique_lock<std::mutex>& lock)
{
    if (!running)
        return ThreadError::NOT_RUNNING;

    if (!worker_thread.joinable())
        return ThreadError::JOIN_FAILED;

    std::thread thread_to_join = std::move(worker_thread);
    running = false;

    lock.unlock();

    thread_to_join.join();

    return ThreadError::SUCCESS;
}

bool ThreadManager::IsRunning() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return running;
}