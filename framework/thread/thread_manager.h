#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include <functional>
#include <thread>
#include <mutex>

#include "stop_token.h"

enum class ThreadError
{
    SUCCESS,
    ALREADY_RUNNING,
    NOT_RUNNING,
    INVALID_ARGUMENT,
    THREAD_CREATION_FAILED,
    JOIN_FAILED
};

class ThreadManager
{
    std::thread worker_thread;
    StopToken stop_token;
    bool running;
    mutable std::mutex mutex;

public:
    ThreadManager();
    ~ThreadManager();

    ThreadError Start(std::function<void(StopToken&)> worker);
    ThreadError Stop();
    ThreadError Join();

    bool IsRunning() const;
};

#endif // THREAD_MANAGER_H
