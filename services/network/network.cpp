#include "network.h"

#include <utility>
#include <cstdint>
#include <vector>
#include <functional>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <poll.h>
#include <netinet/in.h>     
#include <arpa/inet.h>      
#include <fcntl.h>          
#include <thread>           
#include <algorithm>        
#include <cstring>      
#include <pthread.h>   
#include <system_error> 

namespace {
constexpr std::size_t kSendQueueCapacity = 64;
constexpr std::chrono::milliseconds kSendLoopWait{100};
constexpr int kPollTimeoutMs = 100;
constexpr int kListenBacklog = 8;
constexpr int kAcceptPollTimeoutMs = 100;
}

NetworkConnection::NetworkConnection(
                    int fd,
                    std::string peer_address,
                    std::uint16_t peer_port,
                    bool is_server_side,
                    EventBus& bus)

    :   fd_(fd),
        send_queue_(kSendQueueCapacity),
        connected_(true),
        peer_address_(std::move(peer_address)),
        peer_port_(peer_port),
        bus_(bus),
        is_server_side_(is_server_side)
{
}

NetworkConnection::~NetworkConnection() noexcept
{
    Close();

    if (sender_thread_.joinable())
    {
        if (sender_thread_.get_id() == std::this_thread::get_id())
        {
            // We're being destroyed from inside the sender thread's own
            // call stack. Joining self would throw; the thread is
            // already unwinding and will exit as soon as this
            // destructor returns. Detach releases the OS handle.
            sender_thread_.detach();
        }
        else
        {
            sender_thread_.join();
        }
    }
}

bool NetworkConnection::IsConnected() const noexcept
{
    return connected_.load();
}

const std::string& NetworkConnection::PeerAddress() const noexcept
{
    return peer_address_;
}

std::uint16_t NetworkConnection::PeerPort() const noexcept
{
    return peer_port_;
}

NetworkError NetworkConnection::Send(
                const void* data,
                std::size_t size,
                std::chrono::milliseconds timeout)
{

    (void) timeout;

    if (size == 0)
    {
        return NetworkError::INVALID_ARGUMENT;
    }

    if (size > network::kMaxSendChunkSize)
    {
        return NetworkError::INVALID_ARGUMENT;
    }

    if (data == nullptr)
    {
        return NetworkError::INVALID_ARGUMENT;
    }

    if (!connected_.load())
    {
        return NetworkError::SHUTDOWN;
    }

    std::vector<std::uint8_t> chunk(static_cast<const std::uint8_t*>(data),
                                static_cast<const std::uint8_t*>(data) + size);
                            
    const auto qerr = send_queue_.Push(std::move(chunk));

    switch (qerr)
    {
        case QueueError::SUCCESS        : return NetworkError::SUCCESS;
        case QueueError::QUEUE_FULL     : return NetworkError::QUEUE_FULL;
        case QueueError::QUEUE_SHUTDOWN : return NetworkError::SHUTDOWN;
        default                         : return NetworkError::SHUTDOWN;
    }
}

NetworkError NetworkConnection::Send(
                                const std::vector<std::uint8_t>& payload,
                                std::chrono::milliseconds timeout)
{
    return Send(payload.data(), payload.size(), timeout);
}

NetworkError NetworkConnection::Send(
                                const std::string& text,
                                std::chrono::milliseconds timeout)
{
    return Send(text.data(), text.size(), timeout);
}

void NetworkConnection::Teardown(NetworkError reason) noexcept
{
    if (!connected_.exchange(false))
    {
        return;
    }

    stop_requested_.store(true);  

    send_queue_.Shutdown(QueueShutdownMode::IMMEDIATE);

    if (fd_ >= 0)
    {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }

    // shared_from_this() throws if the object is being destroyed
    // (strong count == 0) — which happens on the Start()-failure path
    // in AcceptLoop. weak_from_this() returns an expired weak_ptr in
    // that case, and .lock() returns null. Skip the publish: no
    // NetworkConnected was ever emitted for this connection, so no
    // NetworkDisconnected should be either.


    // Only server-side connections publish NetworkDisconnected.
    // Client-side connections are private to their caller, who already
    // has the shared_ptr and learns of disconnection via Recv/Send
    // returning error codes. Publishing from both sides would create
    // duplicate events and confuse subscribers about which side the
    // disconnect originated on.
    if (!is_server_side_)
    {
        return;
    }

    auto self = weak_from_this().lock();

    if (!self)
    {
        return;
    }

    NetworkDisconnected event;

    event.peer_address  = peer_address_;
    event.peer_port     = peer_port_;
    event.reason        = reason;
    event.connection    = std::move(self);

    bus_.Publish(event);
}

NetworkError NetworkConnection::Start()
{
    stop_requested_.store(false);

    try
    {
        sender_thread_ = std::thread([this]()
        {
            pthread_setname_np(pthread_self(), "net-send");
            SendLoop();
        });
    }
    catch (const std::system_error&)
    {
        return NetworkError::SEND_FAILED;
    }

    return NetworkError::SUCCESS;
}

void NetworkConnection::Close() noexcept
{
    Teardown(NetworkError::SHUTDOWN);
}

void NetworkConnection::SendLoop()
{
    while (!stop_requested_.load())
    {
        std::vector<std::uint8_t> chunk;

        const auto pop = send_queue_.PopFor(chunk, kSendLoopWait);

        if (pop == QueueError::QUEUE_TIMEOUT)   continue;
        if (pop == QueueError::QUEUE_SHUTDOWN)  return;
        if (pop != QueueError::SUCCESS)         return;

        const std::uint8_t* ptr       = chunk.data();
        std::size_t         remaining = chunk.size();

        while (remaining > 0)
        {
            if (stop_requested_.load())
            {
                return;
            }

            const ssize_t n = ::send(fd_, ptr, remaining, MSG_NOSIGNAL);

            if (n > 0)
            {
                ptr       += n;
                remaining -= static_cast<std::size_t>(n);
                continue;
            }

            if (n < 0 && errno == EINTR)
            {
                continue;
            }

            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                pollfd pfd{fd_, POLLOUT, 0};
                const int pr = ::poll(&pfd, 1, kPollTimeoutMs);

                if (pr == 0)  continue;

                if (pr < 0)
                {
                    if (errno == EINTR) continue;

                    Teardown(NetworkError::SEND_FAILED);
                    return;
                }

                if (pfd.revents & (POLLERR | POLLHUP))
                {
                    Teardown(NetworkError::PEER_CLOSED);
                    return;
                }

                continue;
            }

            if (n < 0 && (errno == EPIPE || errno == ECONNRESET))
            {
                Teardown(NetworkError::PEER_CLOSED);
                return;
            }

            Teardown(NetworkError::SEND_FAILED);
            return;
        }
    }
}

NetworkError NetworkConnection::Recv(std::vector<std::uint8_t>& out_payload,
                                     std::size_t max_bytes,
                                     std::chrono::milliseconds timeout)
{
    if (max_bytes == 0)
    {
        return NetworkError::INVALID_ARGUMENT;
    }

    std::lock_guard<std::mutex> lock(recv_mutex_);

    if (!connected_.load())
    {
        return NetworkError::SHUTDOWN;
    }

    // Snapshot fd_ into a local. Teardown() sets it to -1, so reading
    // the member twice could give us two different values.
    const int fd = fd_;

    if (fd < 0)
    {
        return NetworkError::SHUTDOWN;
    }

    // ---- Step 1: wait for data (or timeout) ----
    pollfd pfd{fd, POLLIN, 0};
    const int pr = ::poll(&pfd, 1, static_cast<int>(timeout.count()));

    if (pr == 0)
    {
        return NetworkError::TIMEOUT;
    }

    if (pr < 0)
    {
        if (errno == EINTR)
        {
            // Signal interrupted the wait. Treat as timeout — the
            // caller can retry if they want.
            return NetworkError::TIMEOUT;
        }

        Teardown(NetworkError::RECEIVE_FAILED);
        return NetworkError::RECEIVE_FAILED;
    }

    // poll returned > 0. Check for error/hangup/nval before trusting POLLIN.
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
    {
        Teardown(NetworkError::PEER_CLOSED);
        return NetworkError::PEER_CLOSED;
    }

    // ---- Step 2: single recv — no loop ----
    std::vector<std::uint8_t> buffer(max_bytes);

    const ssize_t n = ::recv(fd, buffer.data(), max_bytes, 0);

    if (n > 0)
    {
        buffer.resize(static_cast<std::size_t>(n));
        out_payload = std::move(buffer);
        return NetworkError::SUCCESS;
    }

    if (n == 0)
    {
        // Clean FIN from the peer.
        Teardown(NetworkError::PEER_CLOSED);
        return NetworkError::PEER_CLOSED;
    }

    // n < 0 — check errno
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
    {
        // Should not normally happen: poll said readable. Treat as
        // timeout so the caller can retry.
        return NetworkError::TIMEOUT;
    }

    if (errno == ECONNRESET)
    {
        Teardown(NetworkError::PEER_CLOSED);
        return NetworkError::PEER_CLOSED;
    }

    Teardown(NetworkError::RECEIVE_FAILED);
    return NetworkError::RECEIVE_FAILED;
}


// ---------------------------------------------------------------------
//                          NETWORK SERVER
// ---------------------------------------------------------------------

NetworkServer::NetworkServer(EventBus& bus) : bus_(bus)
{
}

NetworkServer::~NetworkServer() noexcept
{
    Stop();
}

void NetworkServer::OnDisconnected(const NetworkDisconnected& event)
{
    std::lock_guard<std::mutex> lock(connections_mutex_);

    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [&event](const std::shared_ptr<NetworkConnection>& conn)
            {
                return conn.get() == event.connection.get();
            }),
        connections_.end());
}


NetworkError NetworkServer::Start(const std::string& bind_address,
                                std::uint16_t port)
{
    if (bind_address.empty())
    {
        return NetworkError::INVALID_ARGUMENT;
    }

    if (running_.load())
    {
        return NetworkError::ALREADY_RUNNING;
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return NetworkError::SOCKET_CREATION_FAILED;
    }

    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        ::close(fd);
        return NetworkError::SOCKET_CREATION_FAILED;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);   // host -> network byte order

    if (bind_address == "0.0.0.0")
    {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
        if (::inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1)
        {
            ::close(fd);
            return NetworkError::INVALID_ARGUMENT;
        }
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        ::close(fd);
        return NetworkError::BIND_FAILED;
    }

    if (::listen(fd, kListenBacklog) < 0)
    {
        ::close(fd);
        return NetworkError::LISTEN_FAILED;
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        ::close(fd);
        return NetworkError::SOCKET_CREATION_FAILED;
    }

    disconnect_sub_ = bus_.Subscribe<NetworkDisconnected>(
        [this](const NetworkDisconnected& event) { OnDisconnected(event); });

    if (!disconnect_sub_.IsValid())
    {
        ::close(fd);
        return NetworkError::SUBSCRIPTION_FAILED;
    }

    listen_fd_      = fd;
    bind_address_   = bind_address;
    port_           = port;

    const ThreadError result = accept_thread_.Start(
        "net-accept",
        [this](StopToken& token) { AcceptLoop(token); });

    if (result != ThreadError::SUCCESS)
    {
        disconnect_sub_.Cancel();
        ::close(listen_fd_);
        listen_fd_ = -1;
        return NetworkError::THREAD_CREATION_FAILED;
    }

    running_.store(true);

    return NetworkError::SUCCESS;
}

void NetworkServer::Stop() noexcept
{
    if (!running_.exchange(false))
    {
        return;   // someone already stopped
    }

    // 1. Stop the accept thread. No new connections after this.
    accept_thread_.Stop();
    accept_thread_.Join();

    // 2. Snapshot connections, then close each outside the lock.
    //    Close() fires NetworkDisconnected → OnDisconnected runs on
    //    this thread → takes connections_mutex_. Must not hold it.
    std::vector<std::shared_ptr<NetworkConnection>> to_close;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        to_close = connections_;
    }

    for (auto& conn : to_close)
    {
        conn->Close();
    }

    // to_close goes out of scope here — refs drop. If any were the
    // last ref, the destructor runs on THIS thread (Stop thread, not
    // sender thread) → normal Stop + Join path in ~NetworkConnection.

    // 3. Close the listen fd.
    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    // 4. Cancel the disconnect subscription. Idempotent.
    disconnect_sub_.Cancel();
}

void NetworkServer::AcceptLoop(StopToken& token)
{
    while (!token.IsStopRequested())
    {
        pollfd pfd{listen_fd_, POLLIN, 0};
        const int pr = ::poll(&pfd, 1, kAcceptPollTimeoutMs);

        if (pr == 0) continue;
        if (pr < 0)
        {
            if (errno == EINTR) continue;
            return;
        }
        if (!(pfd.revents & POLLIN)) continue;

        sockaddr_in peer_addr{};
        socklen_t   peer_len = sizeof(peer_addr);

        const int client_fd = ::accept(listen_fd_,
                                   reinterpret_cast<sockaddr*>(&peer_addr),
                                   &peer_len);

        if (client_fd < 0) continue;

        // non-blocking
        const int flags = ::fcntl(client_fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            ::close(client_fd);
            continue;
        }

        // peer identity
        char addr_buf[INET_ADDRSTRLEN] = {0};
        const char* result = ::inet_ntop(AF_INET,
                                     &peer_addr.sin_addr,
                                     addr_buf,
                                     sizeof(addr_buf));

        std::string peer_address = (result != nullptr)
            ? std::string(addr_buf)
            : std::string("unknown");

        const std::uint16_t peer_port = ntohs(peer_addr.sin_port);

        // wrap and start
        NetworkConnection* raw =
            new NetworkConnection(client_fd, peer_address, peer_port, true, bus_);
        std::shared_ptr<NetworkConnection> conn(raw);

        if (conn->Start() != NetworkError::SUCCESS)
        {
            continue;   // conn destructs, fd closed via Teardown
        }

        // register
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_.push_back(conn);
        }

        // notify
        NetworkConnected event;
        event.peer_address = peer_address;
        event.peer_port    = peer_port;
        event.connection   = conn;

        bus_.Publish(event);
    }
}

bool NetworkServer::IsRunning() const noexcept
{
    return running_.load();
}

std::size_t NetworkServer::ConnectionCount() const noexcept
{
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}


// ---------------------------------------------------------------------
//                          NETWORK CLIENT
// ---------------------------------------------------------------------

NetworkClient::NetworkClient(EventBus& bus) : bus_(bus)
{
}

NetworkError NetworkClient::Connect(const std::string& host,
                    std::uint16_t      port,
                    std::chrono::milliseconds timeout,
                    std::shared_ptr<NetworkConnection>& out_connection)
{
    if (host.empty())
    {
        return NetworkError::INVALID_ARGUMENT;
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return NetworkError::SOCKET_CREATION_FAILED;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
    {
        ::close(fd);
        return NetworkError::INVALID_ARGUMENT;
    }

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        ::close(fd);
        return NetworkError::SOCKET_CREATION_FAILED;
    }

    const int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    if (rc < 0 && errno != EINPROGRESS)
    {
        ::close(fd);
        return NetworkError::CONNECT_FAILED;
    }

    if (rc < 0) //EINPROGRESS
    {
        pollfd pfd{fd, POLLOUT, 0};
        const int pr = ::poll(&pfd, 1, static_cast<int>(timeout.count()));

        if (pr == 0)
        {
            ::close(fd);
            return NetworkError::TIMEOUT;
        }

        if (pr < 0)
        {
            ::close(fd);
            return NetworkError::CONNECT_FAILED;
        }

        int so_error    = 0;
        socklen_t len   = sizeof(so_error);

        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0)
        {
            ::close(fd);
            return NetworkError::CONNECT_FAILED;
        }

        if (so_error != 0)
        {
            ::close(fd);
            return NetworkError::CONNECT_FAILED;
        }
    }

    NetworkConnection* raw = new NetworkConnection(fd,
                                                   host,
                                                   port,
                                                   false,
                                                   bus_);
    std::shared_ptr<NetworkConnection> conn(raw);

    if (conn->Start() != NetworkError::SUCCESS)
    {
        return NetworkError::SEND_FAILED;
    }

    // No NetworkConnected publication here. NetworkClient is a factory:
    // the caller receives the connection via out_connection and owns it
    // from that point forward. Publishing would duplicate the server-side
    // event and mislead subscribers about which end initiated the connect.

    out_connection = std::move(conn);

    return NetworkError::SUCCESS;
}
