#include "ipc.h"
#include "ipc_internal.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

IpcClient::IpcClient() = default;

IpcClient::~IpcClient()
{
    Disconnect();
}

IpcError IpcClient::Connect(const std::string& socket_path)
{
    if (socket_path.empty())
        return IpcError::INVALID_ARGUMENT;

    if (socket_.IsValid())
        return IpcError::ALREADY_CONNECTED;

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return IpcError::SOCKET_CREATION_FAILED;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path,
                 socket_path.c_str(),
                 sizeof(addr.sun_path) - 1);

    if (::connect(fd,
                  reinterpret_cast<sockaddr*>(&addr),
                  sizeof(addr)) < 0)
    {
        ::close(fd);
        return IpcError::CONNECT_FAILED;
    }

    if (!ipc_internal::SetNonBlocking(fd))
    {
        ::close(fd);
        return IpcError::SOCKET_CREATION_FAILED;
    }

    socket_ = IpcSocket(fd);
    return IpcError::SUCCESS;
}

IpcError IpcClient::ConnectWithRetry(
    const std::string& socket_path,
    int max_attempts,
    std::chrono::milliseconds initial_delay,
    std::chrono::milliseconds max_delay)
{
    if (max_attempts <= 0)
        return IpcError::INVALID_ARGUMENT;

    auto delay    = initial_delay;
    IpcError last = IpcError::CONNECT_FAILED;

    for (int i = 0; i < max_attempts; ++i)
    {
        last = Connect(socket_path);
        if (last == IpcError::SUCCESS)
            return IpcError::SUCCESS;

        if (i + 1 < max_attempts)
        {
            std::this_thread::sleep_for(delay);
            delay = std::min(delay * 2, max_delay);
        }
    }

    return last;
}

IpcError IpcClient::Send(const void* data,
                         std::size_t size,
                         std::chrono::milliseconds timeout)
{
    return socket_.Send(data, size, timeout);
}

IpcError IpcClient::Send(const std::vector<std::uint8_t>& payload,
                         std::chrono::milliseconds timeout)
{
    return socket_.Send(payload, timeout);
}

IpcError IpcClient::Send(const std::string& text,
                         std::chrono::milliseconds timeout)
{
    return socket_.Send(text, timeout);
}

IpcError IpcClient::Recv(std::vector<std::uint8_t>& out_payload,
                         std::chrono::milliseconds timeout)
{
    return socket_.Recv(out_payload, timeout);
}

void IpcClient::Disconnect() noexcept
{
    socket_.Close();
}

bool IpcClient::IsConnected() const noexcept
{
    return socket_.IsValid();
}