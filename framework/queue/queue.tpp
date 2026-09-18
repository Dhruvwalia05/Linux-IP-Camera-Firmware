#ifndef QUEUE_TPP
#define QUEUE_TPP

#ifndef QUEUE_H
#include "queue.h"
#endif

#include <chrono>

template <typename T>
Queue<T>::Queue(std::size_t capacity)
    : capacity(capacity),
      state(capacity > 0 ? QueueState::RUNNING
                         : QueueState::SHUTDOWN),
      valid(capacity > 0)
{
}

template <typename T>
QueueError Queue<T>::Push(T&& item)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (state != QueueState::RUNNING)
        return QueueError::QUEUE_SHUTDOWN;

    if (items.size() >= capacity)
        return QueueError::QUEUE_FULL;

    items.push(std::move(item));
    not_empty.notify_one();

    return QueueError::SUCCESS;
}

template <typename T>
QueueError Queue<T>::PushFor(T&& item,
                             std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex);

    const bool accepted = not_full.wait_for(
        lock,
        timeout,
        [this]()
        {
            return state != QueueState::RUNNING ||
                   items.size() < capacity;
        });

    if (!accepted)
        return QueueError::QUEUE_TIMEOUT;

    if (state != QueueState::RUNNING)
        return QueueError::QUEUE_SHUTDOWN;

    items.push(std::move(item));
    not_empty.notify_one();

    return QueueError::SUCCESS;
}

template <typename T>
QueueError Queue<T>::Pop(T& item)
{
    std::unique_lock<std::mutex> lock(mutex);

    not_empty.wait(lock, [this]()
    {
        return !items.empty() || state != QueueState::RUNNING;
    });

    if (!items.empty())
    {
        item = std::move(items.front());
        items.pop();

        not_full.notify_one();

        if (state == QueueState::DRAINING && items.empty())
        {
            state = QueueState::SHUTDOWN;
            not_empty.notify_all();
            not_full.notify_all();
        }

        return QueueError::SUCCESS;
    }

    return QueueError::QUEUE_SHUTDOWN;
}

template <typename T>
QueueError Queue<T>::PopFor(T& item,
                            std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex);

    const bool ready = not_empty.wait_for(
        lock,
        timeout,
        [this]()
        {
            return !items.empty() ||
                   state != QueueState::RUNNING;
        });

    if (!ready)
        return QueueError::QUEUE_TIMEOUT;

    if (!items.empty())
    {
        item = std::move(items.front());
        items.pop();

        not_full.notify_one();

        if (state == QueueState::DRAINING && items.empty())
        {
            state = QueueState::SHUTDOWN;
            not_empty.notify_all();
            not_full.notify_all();
        }

        return QueueError::SUCCESS;
    }

    return QueueError::QUEUE_SHUTDOWN;
}

template <typename T>
QueueError Queue<T>::Shutdown(QueueShutdownMode mode)
{
    std::lock_guard<std::mutex> lock(mutex);

    if (state == QueueState::SHUTDOWN)
        return QueueError::QUEUE_SHUTDOWN;

    if (mode == QueueShutdownMode::IMMEDIATE)
    {
        while (!items.empty())
            items.pop();

        state = QueueState::SHUTDOWN;

        not_empty.notify_all();
        not_full.notify_all();

        return QueueError::SUCCESS;
    }

    state = QueueState::DRAINING;

    if (items.empty())
        state = QueueState::SHUTDOWN;

    not_empty.notify_all();
    not_full.notify_all();

    return QueueError::SUCCESS;
}

template <typename T>
bool Queue<T>::IsShutdown() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return state == QueueState::SHUTDOWN;
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

#endif // QUEUE_TPP