#ifndef QUEUE_H
#include "queue.h"
#endif

template <typename T>
Queue<T>::Queue(std::size_t capacity)
    :   capacity(capacity),
        state (capacity > 0 ? QueueState::RUNNING
                            : QueueState::SHUTDOWN),
        valid(capacity > 0)
{
}

template <typename T>
QueueError Queue<T>::Push(T&& item)
{
    std::lock_guard<std::mutex> lock(mutex);

    if(state != QueueState::RUNNING)
    {
        return QueueError::QUEUE_SHUTDOWN;
    }

    if(items.size() >= capacity)
    {
        return QueueError::QUEUE_FULL;
    }

    items.push(std::move(item));

    condition.notify_one();

    return QueueError::SUCCESS;
}

template <typename T>
QueueError Queue<T>::Pop(T& item)
{
    std::unique_lock<std::mutex> lock(mutex);

    condition.wait(lock, [this]()
    {
        return !items.empty() || state != QueueState::RUNNING;
    });

    if (!items.empty())
    {
        item = std::move(items.front());
        items.pop();

        if (state == QueueState::DRAINING && items.empty())
        {
            state = QueueState::SHUTDOWN;
            condition.notify_all();
        }

        return QueueError::SUCCESS;
    }

    return QueueError::QUEUE_SHUTDOWN;
}

template <typename T>
QueueError Queue<T>::Shutdown(QueueShutdownMode mode)
{
    std::lock_guard<std::mutex> lock(mutex);

    if(state == QueueState::SHUTDOWN)
    {
        return QueueError::QUEUE_SHUTDOWN;
    }

    if(mode == QueueShutdownMode::IMMEDIATE)
    {
        while (!items.empty())
        {
            items.pop();
        }

        state = QueueState::SHUTDOWN;

        condition.notify_all();

        return QueueError::SUCCESS;
    }

    state = QueueState::DRAINING;

    if(items.empty())
    {
        state = QueueState::SHUTDOWN;
    }
    
    condition.notify_all();

    return QueueError::SUCCESS;
}

template <typename T>
bool Queue<T>::IsShutdown() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return QueueState::SHUTDOWN == state;
}

template <typename T>
bool Queue<T>::IsValid() const
{
    return valid;
}

template <typename T>
std::size_t Queue<T>::Size() const
{
    std::lock_guard<std::mutex> lock(mutex);

    return items.size();
}

template <typename T>
std::size_t Queue<T>::Capacity() const
{
    return capacity;
}
