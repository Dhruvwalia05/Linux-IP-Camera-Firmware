#ifndef QUEUE_H
#define QUEUE_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

enum class QueueError
{
    SUCCESS,
    QUEUE_FULL,
    QUEUE_SHUTDOWN,
    QUEUE_TIMEOUT
};

enum class QueueShutdownMode
{
    IMMEDIATE,
    DRAIN
};

template <typename T>
class Queue
{
public:
    explicit Queue(std::size_t capacity);

    Queue(const Queue&)            = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&)                 = delete;
    Queue& operator=(Queue&&)      = delete;

    QueueError Push(T&& item);

    QueueError PushFor(T&& item,
                       std::chrono::milliseconds timeout);

    QueueError Pop(T& item);

    QueueError PopFor(T& item,
                      std::chrono::milliseconds timeout);

    QueueError Shutdown(QueueShutdownMode mode);

    bool        IsShutdown() const;
    bool        IsValid()    const;
    std::size_t Size()       const;
    std::size_t Capacity()   const;

private:
    enum class QueueState
    {
        RUNNING,
        DRAINING,
        SHUTDOWN
    };

    std::queue<T>           items;
    std::size_t             capacity;
    QueueState              state;
    bool                    valid;

    mutable std::mutex      mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
};

#include "queue.tpp"

#endif // QUEUE_H