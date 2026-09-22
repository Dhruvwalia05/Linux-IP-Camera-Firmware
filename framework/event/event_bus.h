#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

enum class EventError
{
    SUCCESS,
    INVALID_ARGUMENT,
    SUBSCRIBER_LIMIT_REACHED,
    REENTRANT_PUBLISH,
    NOT_FOUND
};

class EventBus;
class Subscription;

// ---------------------------------------------------------------------------
// EventBus
// ---------------------------------------------------------------------------
//
// A synchronous, in-process, type-keyed event dispatcher.
//
// Design summary (see commit message for full rationale):
//
//   * Delivery is synchronous on the publisher's thread.
//   * Events are identified by C++ type, not string topics.
//   * Subscriptions are RAII: destroying the Subscription handle
//     cancels the subscription. Discarding the return of Subscribe()
//     destroys the subscription immediately — [[nodiscard]] makes
//     that misuse a compile-time error.
//   * A subscriber callback that throws is caught and logged; the
//     subscription is kept, and remaining subscribers still receive
//     the event.
//   * Re-entrant Publish (from inside a subscriber callback) is
//     rejected with REENTRANT_PUBLISH.
//   * Delivery order equals subscription order.
//   * Capacity is bounded to kMaxSubscribers total subscribers.
//
// Not supported (deliberately):
//   * Asynchronous dispatch
//   * Cross-process delivery (see IPC component)
//   * Delivery persistence or retry
//   * Priority ordering
//

class EventBus
{
public:
    static constexpr std::size_t kMaxSubscribers = 128;

    EventBus();
    ~EventBus() = default;

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&)                 = delete;
    EventBus& operator=(EventBus&&)      = delete;

    // The returned Subscription owns the registration. Discarding
    // the handle without keeping it alive destroys the subscription
    // immediately at the end of the full expression. [[nodiscard]]
    // turns that misuse into a compile-time error.
    template <typename E>
    [[nodiscard]] Subscription Subscribe(std::function<void(const E&)> callback);

    template <typename E>
    EventError Publish(const E& event);

    std::size_t SubscriberCount() const;

private:
    friend class Subscription;

    // -- Type erasure machinery ---------------------------------------------

    class SubscriberBase
    {
    public:
        virtual ~SubscriberBase() = default;
        virtual void Call(const void* event) = 0;
    };

    template <typename E>
    class SubscriberImpl : public SubscriberBase
    {
    public:
        explicit SubscriberImpl(std::function<void(const E&)> fn)
            : fn_(std::move(fn))
        {
        }

        void Call(const void* event) override
        {
            // Safe because SubscriberImpl<E> is only ever inserted
            // into the vector keyed by typeid(E). The type of the
            // pointer passed to Call() matches E by construction.
            fn_(*static_cast<const E*>(event));
        }

    private:
        std::function<void(const E&)> fn_;
    };

    struct SubscriberEntry
    {
        std::uint64_t                   id = 0;
        std::shared_ptr<SubscriberBase> subscriber;
    };

    // -- Internal shared state ----------------------------------------------
    //
    // Held via shared_ptr so that a Subscription handle outliving the
    // EventBus can detect that the bus is gone and skip cleanup
    // safely.

    struct State
    {
        mutable std::mutex mutex;

        std::unordered_map<
            std::type_index,
            std::vector<SubscriberEntry>> subscribers;

        std::uint64_t next_id           = 1;
        std::size_t   total_subscribers = 0;
    };

    static void RemoveSubscriberFromState(State&        state,
                                          std::uint64_t id);

    std::shared_ptr<State> state_;
};

// ---------------------------------------------------------------------------
// Subscription
// ---------------------------------------------------------------------------
//
// RAII handle to a single subscriber registration. On destruction
// (or explicit Cancel()), the subscription is removed from its bus.
//
// Move-only. Default-constructed Subscription is invalid.
//

class Subscription
{
public:
    Subscription() noexcept = default;
    ~Subscription();

    Subscription(const Subscription&)            = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    void Cancel();

private:
    friend class EventBus;

    Subscription(std::weak_ptr<EventBus::State> state,
                 std::uint64_t                  id) noexcept;

    std::weak_ptr<EventBus::State> state_;
    std::uint64_t                  id_ = 0;
};

// ---------------------------------------------------------------------------
// Subscription — inline implementation
// ---------------------------------------------------------------------------

inline Subscription::Subscription(std::weak_ptr<EventBus::State> state,
                                  std::uint64_t                  id) noexcept
    : state_(std::move(state)), id_(id)
{
}

inline Subscription::~Subscription()
{
    Cancel();
}

inline Subscription::Subscription(Subscription&& other) noexcept
    : state_(std::move(other.state_)), id_(other.id_)
{
    other.id_ = 0;
}

inline Subscription& Subscription::operator=(Subscription&& other) noexcept
{
    if (this != &other)
    {
        Cancel();
        state_    = std::move(other.state_);
        id_       = other.id_;
        other.id_ = 0;
    }
    return *this;
}

inline bool Subscription::IsValid() const noexcept
{
    return !state_.expired() && id_ != 0;
}

inline void Subscription::Cancel()
{
    if (auto state = state_.lock())
    {
        EventBus::RemoveSubscriberFromState(*state, id_);
    }

    state_.reset();
    id_ = 0;
}

// ---------------------------------------------------------------------------
// EventBus — non-template inline implementation
// ---------------------------------------------------------------------------

inline EventBus::EventBus()
    : state_(std::make_shared<State>())
{
}

inline void EventBus::RemoveSubscriberFromState(State&        state,
                                                std::uint64_t id)
{
    std::lock_guard<std::mutex> lock(state.mutex);

    for (auto& [type_index, entries] : state.subscribers)
    {
        for (auto it = entries.begin(); it != entries.end(); ++it)
        {
            if (it->id == id)
            {
                entries.erase(it);
                --state.total_subscribers;
                return;
            }
        }
    }
}

inline std::size_t EventBus::SubscriberCount() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->total_subscribers;
}

// ---------------------------------------------------------------------------
// EventBus — template implementation
// ---------------------------------------------------------------------------

namespace event_bus_detail
{
    // Thread-local flag: true while this thread is inside a Publish
    // dispatch. Used to reject re-entrant Publish.
    //
    // Shared across all EventBus instances on the same thread. This
    // is a deliberate simplification: cross-bus publish from within a
    // dispatch is also rejected. Per-instance tracking can be added
    // later if a concrete need appears.
    inline thread_local bool dispatching = false;
}

template <typename E>
[[nodiscard]] Subscription EventBus::Subscribe(
    std::function<void(const E&)> callback)
{
    if (!callback)
    {
        std::fprintf(stderr,
                     "[EventBus] Subscribe called with empty callback\n");
        return Subscription{};
    }

    std::lock_guard<std::mutex> lock(state_->mutex);

    if (state_->total_subscribers >= kMaxSubscribers)
    {
        std::fprintf(stderr,
                     "[EventBus] subscriber limit reached (%zu)\n",
                     kMaxSubscribers);
        return Subscription{};
    }

    const std::uint64_t id = state_->next_id++;

    SubscriberEntry entry;
    entry.id         = id;
    entry.subscriber =
        std::make_shared<SubscriberImpl<E>>(std::move(callback));

    state_->subscribers[std::type_index(typeid(E))]
        .push_back(std::move(entry));

    ++state_->total_subscribers;

    return Subscription(state_, id);
}

template <typename E>
EventError EventBus::Publish(const E& event)
{
    if (event_bus_detail::dispatching)
    {
        return EventError::REENTRANT_PUBLISH;
    }

    // Snapshot the subscriber list under the lock. Snapshot holds
    // shared_ptr copies so entries stay alive even if a callback
    // cancels a subscription mid-dispatch.
    std::vector<std::shared_ptr<SubscriberBase>> snapshot;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);

        auto it = state_->subscribers.find(std::type_index(typeid(E)));
        if (it == state_->subscribers.end())
        {
            return EventError::SUCCESS;   // no subscribers
        }

        snapshot.reserve(it->second.size());
        for (const auto& entry : it->second)
        {
            snapshot.push_back(entry.subscriber);
        }
    }

    // Dispatch without holding the lock. A subscriber may Subscribe,
    // Cancel, or touch other subsystems without deadlocking.
    event_bus_detail::dispatching = true;

    for (const auto& sub : snapshot)
    {
        try
        {
            sub->Call(&event);
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr,
                         "[EventBus] subscriber threw std::exception: %s\n",
                         e.what());
        }
        catch (...)
        {
            std::fprintf(stderr,
                         "[EventBus] subscriber threw unknown exception\n");
        }
    }

    event_bus_detail::dispatching = false;

    return EventError::SUCCESS;
}

#endif // EVENT_BUS_H