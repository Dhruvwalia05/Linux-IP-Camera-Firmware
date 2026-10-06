#ifndef NETWORK_H
#define NETWORK_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <thread>

#include "framework/event/event_bus.h"
#include "framework/queue/queue.h"
#include "framework/thread/thread_manager.h"

// ===========================================================================
// Network
// ===========================================================================
//
// TCP transport layer for the firmware. Provides:
//
//   * NetworkServer    — binds to an address, listens, accepts incoming
//                        connections. Publishes NetworkConnected for each
//                        new peer.
//   * NetworkClient    — one-shot factory that initiates an outgoing
//                        connection. Publishes NetworkConnected on success.
//   * NetworkConnection — shared handle to a single live connection.
//                        Owns the fd, the send queue, and the sender
//                        thread. Publishes NetworkDisconnected when the
//                        connection ends, for any reason.
//
// Design summary:
//
//   * Thread-per-connection. Accept runs on one thread; each connection
//     has one sender thread (via ThreadManager). Reader work happens on
//     the caller's thread via Recv(). Bounded by the number of peers,
//     which for an IP camera is small (< 20 in typical use).
//
//   * Send is queue-based. Callers push a chunk of bytes; a dedicated
//     sender thread drains the queue and writes to the socket. When the
//     queue is full, Send returns QUEUE_FULL — the caller decides
//     whether to block, drop, or disconnect. No hidden drop policy.
//
//   * No framing. Network moves bytes. Message boundaries belong to the
//     protocol layer (RTSP, MQTT, HTTP). Adding framing here would
//     double-frame MQTT packets.
//
//   * Events, not callbacks. New connections are published as
//     NetworkConnected. Connection ends are published as
//     NetworkDisconnected with a NetworkError reason. Services subscribe
//     via EventBus.
//
//   * Bounded graceful shutdown. NetworkServer::Stop() signals accept,
//     closes connections, waits up to kShutdownGracePeriodMs for
//     graceful exit, then force-closes remaining connections and joins.
//     Stop() never blocks indefinitely.
//
// Not supported (deliberately):
//   * TLS (deferred to Phase 4; will be added via an ITransport
//     abstraction without changing the public API)
//   * UDP
//   * Retry / reconnect policy (caller's concern)
//   * Connection pooling (one connection per peer)
//   * Async Recv (callers use their own threads if needed)
//

namespace network
{

// Any Send() call with size > kMaxSendChunkSize returns INVALID_ARGUMENT.
// Prevents a buggy caller from queuing hundreds of MB into a bounded queue.
// Callers that need to send more must split the payload.
constexpr std::size_t kMaxSendChunkSize = 256 * 1024;   // 256 KB

// NetworkServer::Stop() waits this long for connections to close
// gracefully before force-closing them.
constexpr int kShutdownGracePeriodMs = 5000;            // 5 s

} // namespace network

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

enum class NetworkError
{
    SUCCESS,
    INVALID_ARGUMENT,
    ALREADY_RUNNING,
    NOT_RUNNING,
    ALREADY_CONNECTED,
    SOCKET_CREATION_FAILED,
    BIND_FAILED,
    LISTEN_FAILED,
    ACCEPT_FAILED,
    CONNECT_FAILED,
    SEND_FAILED,
    RECEIVE_FAILED,
    TIMEOUT,
    PEER_CLOSED,
    QUEUE_FULL,
    SHUTDOWN,
    THREAD_CREATION_FAILED,
    SUBSCRIPTION_FAILED
};

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

class NetworkConnection;

struct NetworkConnected
{
    std::string                        peer_address;
    std::uint16_t                      peer_port = 0;
    std::shared_ptr<NetworkConnection> connection;
};

struct NetworkDisconnected
{
    std::string                        peer_address;
    std::uint16_t                      peer_port = 0;
    NetworkError                       reason = NetworkError::PEER_CLOSED;
    std::shared_ptr<NetworkConnection> connection;
};

// ---------------------------------------------------------------------------
// NetworkConnection
// ---------------------------------------------------------------------------
//
// Shared handle to a single live TCP connection.
//
// Constructed only by NetworkServer (on accept) and NetworkClient (on
// connect). Services receive it via the NetworkConnected event and hold
// it as a shared_ptr. The object outlives the underlying fd — closing
// the socket makes the connection unusable but does not destroy the
// object while references remain.
//
// Thread safety:
//   Send, Close, IsConnected, PeerAddress, PeerPort   — any thread
//   Recv                                             — one thread at a time
//   The sender thread runs SendLoop() and must not be joined from
//   itself. The destructor joins it; never destroy from inside a
//   callback triggered by the sender thread.
//

class NetworkConnection : public std::enable_shared_from_this<NetworkConnection>
{
public:
    // -- I/O ------------------------------------------------------------

    // Enqueue a chunk of bytes for transmission. Returns:
    //   SUCCESS            — chunk queued
    //   QUEUE_FULL         — send queue at capacity; caller decides
    //   SHUTDOWN           — connection is closing or closed
    //   INVALID_ARGUMENT   — size == 0 or size > kMaxSendChunkSize,
    //                        or data == nullptr with size > 0
    // Timeout is reserved for future use; currently unused because
    // enqueue is non-blocking.
    NetworkError Send(const void* data,
                      std::size_t size,
                      std::chrono::milliseconds timeout);

    NetworkError Send(const std::vector<std::uint8_t>& payload,
                      std::chrono::milliseconds timeout);

    NetworkError Send(const std::string& text,
                      std::chrono::milliseconds timeout);

    // Read up to max_bytes into out_payload. Returns:
    //   SUCCESS            — bytes placed in out_payload
    //   TIMEOUT            — no bytes within timeout
    //   PEER_CLOSED        — remote end closed
    //   SHUTDOWN           — Close() called during the wait
    //   RECEIVE_FAILED     — other OS-level error
    //   INVALID_ARGUMENT   — max_bytes == 0
    //
    // out_payload is left unmodified on any non-SUCCESS return.
    // Only one thread may call Recv at a time; enforced by recv_mutex_.
    NetworkError Recv(std::vector<std::uint8_t>& out_payload,
                      std::size_t max_bytes,
                      std::chrono::milliseconds timeout);

    // -- State ----------------------------------------------------------

    bool               IsConnected() const noexcept;
    const std::string& PeerAddress() const noexcept;
    std::uint16_t      PeerPort()    const noexcept;

    // -- Lifecycle ------------------------------------------------------

    // Close the fd and shut down the send queue. Idempotent. Wakes any
    // blocked Recv or SendLoop. Does not join the sender thread — that
    // happens in the destructor, on whatever thread destroys the last
    // shared_ptr. Safe to call from within the sender thread.
    void Close() noexcept;

    ~NetworkConnection() noexcept;

private:
    friend class NetworkServer;
    friend class NetworkClient;


    // is_server_side == true  → created by NetworkServer::AcceptLoop;
    //                           publishes NetworkDisconnected on teardown.
    // is_server_side == false → created by NetworkClient::Connect;
    //                           stays silent — the caller owns the
    //                           connection and detects death via Recv
    //                           returning PEER_CLOSED or Send returning
    //                           SHUTDOWN.
    NetworkConnection(int fd,
                      std::string peer_address,
                      std::uint16_t peer_port,
                      bool is_server_side,
                      EventBus& bus);

    NetworkConnection(const NetworkConnection&)            = delete;
    NetworkConnection& operator=(const NetworkConnection&) = delete;
    NetworkConnection(NetworkConnection&&)                 = delete;
    NetworkConnection& operator=(NetworkConnection&&)      = delete;

    // Sender thread entry point. Drains send_queue_ and writes to fd_.
    void SendLoop();

    // Start the sender thread. Called by the factory (Server/Client)
    // after the object is wrapped in a shared_ptr — required because
    // SendLoop uses shared_from_this() and that fails during
    // construction. Returns SUCCESS, or SEND_FAILED if the thread
    // cannot be spawned.
    NetworkError Start();

    // Handles connection teardown exactly once per connection.
    //
    // The first caller — whether Close(), SendLoop on socket error,
    // or Recv on peer close — performs the following atomically:
    //   1. Flips connected_ to false via exchange (guards exactly-once)
    //   2. Shuts down the send queue (wakes any blocked Send)
    //   3. Shutdown + closes the fd (wakes any blocked Recv or SendLoop)
    //   4. Publishes NetworkDisconnected with the given reason
    //
    // Subsequent callers return immediately. Safe from any thread.
    void Teardown(NetworkError reason) noexcept;

    int                              fd_ = -1;
    Queue<std::vector<std::uint8_t>> send_queue_;
    std::thread                      sender_thread_;
    std::atomic<bool>                stop_requested_{false};
    std::atomic<bool>                connected_{false};
    std::string                      peer_address_;
    std::uint16_t                    peer_port_ = 0;
    EventBus&                        bus_;
    std::mutex                       recv_mutex_;
    bool                             is_server_side_;
};

// ---------------------------------------------------------------------------
// NetworkServer
// ---------------------------------------------------------------------------
//
// Binds to an address, listens, accepts incoming TCP connections.
//
// Lifecycle:
//     Start()  → bind + listen + spawn accept thread
//     Stop()   → signal accept, close connections (bounded), join
//
// A new peer is published as NetworkConnected with a shared_ptr to the
// new NetworkConnection. When the connection ends (any reason), the
// connection itself publishes NetworkDisconnected. The server subscribes
// to that event internally to remove the connection from its tracking
// list.
//
// Non-copyable, non-movable. Owns a listen fd and a running thread.
//

class NetworkServer
{
public:
    explicit NetworkServer(EventBus& bus);
    ~NetworkServer() noexcept;

    NetworkServer(const NetworkServer&)            = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;
    NetworkServer(NetworkServer&&)                 = delete;
    NetworkServer& operator=(NetworkServer&&)      = delete;

    // Bind, listen, and start the accept thread. Returns:
    //   SUCCESS                  — server running
    //   ALREADY_RUNNING          — Start() already succeeded
    //   INVALID_ARGUMENT         — bind_address is empty
    //   SOCKET_CREATION_FAILED   — ::socket() failed
    //   BIND_FAILED              — ::bind() failed (port in use, ...)
    //   LISTEN_FAILED            — ::listen() failed
    NetworkError Start(const std::string& bind_address,
                       std::uint16_t      port);

    // Signal shutdown, close all connections gracefully (bounded by
    // kShutdownGracePeriodMs), force-close remaining, join accept thread.
    // Idempotent. Safe to call from the destructor.
    void Stop() noexcept;

    bool        IsRunning()       const noexcept;
    std::size_t ConnectionCount() const noexcept;

private:
    // Accept thread entry point. Polls listen_fd_ with a short timeout
    // so it can observe token.IsStopRequested() between accepts.
    void AcceptLoop(StopToken& token);

    // Subscribes to NetworkDisconnected; removes the connection from
    // connections_ on receipt.
    void OnDisconnected(const NetworkDisconnected& event);

    EventBus&                                        bus_;
    Subscription                                     disconnect_sub_;
    int                                              listen_fd_ = -1;
    ThreadManager                                    accept_thread_;
    std::string                                      bind_address_;
    std::uint16_t                                    port_ = 0;
    std::atomic<bool>                                running_{false};
    std::vector<std::shared_ptr<NetworkConnection>>  connections_;
    mutable std::mutex                               connections_mutex_;
};

// ---------------------------------------------------------------------------
// NetworkClient
// ---------------------------------------------------------------------------
//
// One-shot factory for outgoing TCP connections. Holds no connection
// state itself — the caller receives the connection via the out-param
// and owns it from that point forward.
//
// Publishes NetworkConnected on success. Later disconnect events come
// from the connection itself, not from the client.
//
// Non-copyable, non-movable.
//

class NetworkClient
{
public:
    explicit NetworkClient(EventBus& bus);
    ~NetworkClient() = default;

    NetworkClient(const NetworkClient&)            = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;
    NetworkClient(NetworkClient&&)                 = delete;
    NetworkClient& operator=(NetworkClient&&)      = delete;

    // Single connect attempt. On success, out_connection receives a
    // shared_ptr to the new NetworkConnection. On failure, out_connection
    // is untouched. No event is published — the caller owns the
    // connection directly.
    //
    // Returns:
    //   SUCCESS                  — connected
    //   INVALID_ARGUMENT         — host is empty or not a valid dotted-quad
    //                              IPv4 address (no DNS resolution)
    //   SOCKET_CREATION_FAILED   — ::socket() failed
    //   CONNECT_FAILED           — ::connect() failed
    //   TIMEOUT                  — connect did not complete within timeout
    NetworkError Connect(const std::string& host,
                         std::uint16_t      port,
                         std::chrono::milliseconds timeout,
                         std::shared_ptr<NetworkConnection>& out_connection);

private:
    EventBus& bus_;
};

#endif // NETWORK_H
