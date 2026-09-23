#include "ipc.h"
#include "ipc_internal.h"

#include <cerrno>
#include <chrono>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

IpcServer::IpcServer() = default;

IpcServer::~IpcServer()
{
    Stop();
}

IpcError IpcServer::Start(const std::string& socket_path)
{
    if (socket_path.empty())
        return IpcError::INVALID_ARGUMENT;

    if (running_)
        return IpcError::ALREADY_RUNNING;

    // -------------------------------------------------------------------
    // 1. Acquire exclusive lock on <path>.lock
    // -------------------------------------------------------------------
    const std::string lock_path = socket_path + ".lock";

    const int lock_fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR, 0600);
    if (lock_fd < 0)
        return IpcError::SOCKET_CREATION_FAILED;

    if (::flock(lock_fd, LOCK_EX | LOCK_NB) < 0)
    {
        ::close(lock_fd);
        return (errno == EWOULDBLOCK)
            ? IpcError::ALREADY_RUNNING
            : IpcError::BIND_FAILED;
    }

    // -------------------------------------------------------------------
    // 2. We hold the lock. Any existing socket file is stale.
    // -------------------------------------------------------------------
    ::unlink(socket_path.c_str());

    // -------------------------------------------------------------------
    // 3. Create socket
    // -------------------------------------------------------------------
    const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        ::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return IpcError::SOCKET_CREATION_FAILED;
    }

    // -------------------------------------------------------------------
    // 4. Bind
    // -------------------------------------------------------------------
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path,
                 socket_path.c_str(),
                 sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd,
               reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) < 0)
    {
        ::close(listen_fd);
        ::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return IpcError::BIND_FAILED;
    }

    // -------------------------------------------------------------------
    // 5. Restrict file permissions — bind() uses umask by default
    // -------------------------------------------------------------------
    ::chmod(socket_path.c_str(), 0600);

    // -------------------------------------------------------------------
    // 6. Listen
    // -------------------------------------------------------------------
    if (::listen(listen_fd, 4) < 0)
    {
        ::close(listen_fd);
        ::unlink(socket_path.c_str());
        ::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return IpcError::LISTEN_FAILED;
    }

    // -------------------------------------------------------------------
    // 7. Non-blocking listener — needed so Accept() can poll()
    // -------------------------------------------------------------------
    if (!ipc_internal::SetNonBlocking(listen_fd))
    {
        ::close(listen_fd);
        ::unlink(socket_path.c_str());
        ::flock(lock_fd, LOCK_UN);
        ::close(lock_fd);
        return IpcError::SOCKET_CREATION_FAILED;
    }

    // -------------------------------------------------------------------
    // 8. Commit
    // -------------------------------------------------------------------
    listen_fd_   = listen_fd;
    lock_fd_     = lock_fd;
    socket_path_ = socket_path;
    running_     = true;

    return IpcError::SUCCESS;
}

IpcError IpcServer::Accept(std::chrono::milliseconds timeout)
{
    if (!running_)
        return IpcError::NOT_CONNECTED;

    if (client_.IsValid())
        return IpcError::ALREADY_CONNECTED;

    pollfd pfd{listen_fd_, POLLIN, 0};
    const int r = ::poll(&pfd, 1, static_cast<int>(timeout.count()));

    if (r == 0)
        return IpcError::TIMEOUT;

    if (r < 0)
    {
        if (errno == EINTR)
            return IpcError::TIMEOUT;
        return IpcError::RECEIVE_FAILED;
    }

    const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
    if (client_fd < 0)
        return IpcError::RECEIVE_FAILED;

    if (!ipc_internal::SetNonBlocking(client_fd))
    {
        ::close(client_fd);
        return IpcError::RECEIVE_FAILED;
    }

    client_ = IpcSocket(client_fd);
    return IpcError::SUCCESS;
}

IpcError IpcServer::Send(const void* data,
                         std::size_t size,
                         std::chrono::milliseconds timeout)
{
    if (!client_.IsValid())
        return IpcError::NOT_CONNECTED;

    return client_.Send(data, size, timeout);
}

IpcError IpcServer::Send(const std::vector<std::uint8_t>& payload,
                         std::chrono::milliseconds timeout)
{
    if (!client_.IsValid())
        return IpcError::NOT_CONNECTED;

    return client_.Send(payload, timeout);
}

IpcError IpcServer::Send(const std::string& text,
                         std::chrono::milliseconds timeout)
{
    if (!client_.IsValid())
        return IpcError::NOT_CONNECTED;

    return client_.Send(text, timeout);
}

IpcError IpcServer::Recv(std::vector<std::uint8_t>& out_payload,
                         std::chrono::milliseconds timeout)
{
    if (!client_.IsValid())
        return IpcError::NOT_CONNECTED;

    return client_.Recv(out_payload, timeout);
}

void IpcServer::Disconnect() noexcept
{
    client_.Close();
}

bool IpcServer::IsRunning() const noexcept
{
    return running_;
}

bool IpcServer::HasClient() const noexcept
{
    return client_.IsValid();
}

void IpcServer::Stop() noexcept
{
    client_.Close();

    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (!socket_path_.empty())
    {
        ::unlink(socket_path_.c_str());
        socket_path_.clear();
    }

    if (lock_fd_ >= 0)
    {
        ::flock(lock_fd_, LOCK_UN);
        ::close(lock_fd_);
        lock_fd_ = -1;
    }

    running_ = false;
}