#include "thread_manager.h"

#include <functional>
#include <system_error>
#include <exception>

ThreadManager::ThreadManager()
    : running(false)
{
}

ThreadManager::~ThreadManager()
{
    if(worker_thread.joinable())
        std::terminate();
}

ThreadError ThreadManager::Start(std::function<void(StopToken&)> worker)
{
    if (!worker)
        return ThreadError::INVALID_ARGUMENT;

    std::lock_guard<std::mutex> lock(mutex);

    if (running)
        return ThreadError::ALREADY_RUNNING;

    stop_token.Reset();

    try{
        worker_thread = std::thread(worker, std::ref(stop_token));
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
    std::thread thread_to_join;

    {
        std::lock_guard<std::mutex> lock(mutex);

        if(!running)
            return ThreadError::NOT_RUNNING;

        if(!worker_thread.joinable())
            return ThreadError::JOIN_FAILED;

        thread_to_join = std::move(worker_thread);

        running = false;
    }

    thread_to_join.join();

    return ThreadError::SUCCESS;
}

bool ThreadManager::IsRunning() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return running;
}