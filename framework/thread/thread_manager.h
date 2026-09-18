#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "stop_token.h"

enum class ThreadError
{
    SUCCESS,
    ALREADY_RUNNING,
    NOT_RUNNING,
    INVALID_ARGUMENT,
    THREAD_CREATION_FAILED,
    JOIN_FAILED,
    JOIN_TIMEOUT
};

class ThreadManager
{
public:
    ThreadManager();
    ~ThreadManager();

    ThreadManager(const ThreadManager&)            = delete;
    ThreadManager& operator=(const ThreadManager&) = delete;
    ThreadManager(ThreadManager&&)                 = delete;
    ThreadManager& operator=(ThreadManager&&)      = delete;

    ThreadError Start(std::function<void(StopToken&)> worker);
    ThreadError Start(const std::string& name,
                      std::function<void(StopToken&)> worker);

    ThreadError Stop();

    ThreadError Join();
    ThreadError Join(std::chrono::milliseconds timeout);

    bool IsRunning() const;

private:
    ThreadError CompleteJoin(std::unique_lock<std::mutex>& lock);

    std::thread             worker_thread;
    StopToken               stop_token;
    bool                    running;
    bool                    worker_finished;   // guarded by mutex
    std::string             name;

    mutable std::mutex      mutex;
    std::condition_variable completion_cv;
};

#endif // THREAD_MANAGER_H