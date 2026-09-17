#ifndef STOP_TOKEN_H
#define STOP_TOKEN_H

#include <atomic>

class ThreadManager;

class StopToken
{
    friend class ThreadManager;

    std::atomic<bool> stop_requested;

    void RequestStop();
    void Reset();

public:
    StopToken();

    bool IsStopRequested() const;
};

#endif // STOP_TOKEN_H