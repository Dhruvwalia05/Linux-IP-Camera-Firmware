#include "ipc.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

void EncodeHeader(std::uint32_t len, std::uint8_t out[4])
{
    out[0] = static_cast<std::uint8_t>((len >> 24) & 0xFF);
    out[1] = static_cast<std::uint8_t>((len >> 16) & 0xFF);
    out[2] = static_cast<std::uint8_t>((len >>  8) & 0xFF);
    out[3] = static_cast<std::uint8_t>( len        & 0xFF);
}

std::uint32_t DecodeHeader(const std::uint8_t in[4])
{
    return  (static_cast<std::uint32_t>(in[0]) << 24)
          | (static_cast<std::uint32_t>(in[1]) << 16)
          | (static_cast<std::uint32_t>(in[2]) <<  8)
          |  static_cast<std::uint32_t>(in[3]);
}

IpcError WriteAll(int fd,
                  const void* data,
                  std::size_t size,
                  std::chrono::steady_clock::time_point deadline)
{
    const auto* p       = static_cast<const std::uint8_t*>(data);
    std::size_t remaining = size;

    while (remaining > 0)
    {
        const ssize_t n = ::send(fd, p, remaining, MSG_NOSIGNAL);

        if (n > 0)
        {
            p         += n;
            remaining -= static_cast<std::size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR)
            continue;

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                return IpcError::TIMEOUT;

            const auto remaining_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now);

            pollfd pfd{fd, POLLOUT, 0};
            const int r = ::poll(&pfd, 1,
                                 static_cast<int>(remaining_ms.count()));

            if (r == 0)                   return IpcError::TIMEOUT;
            if (r < 0 && errno == EINTR)  continue;
            if (r < 0)                    return IpcError::SEND_FAILED;
            continue;
        }

        if (n < 0 && errno == EPIPE)
            return IpcError::PEER_CLOSED;

        return IpcError::SEND_FAILED;
    }

    return IpcError::SUCCESS;
}

IpcError ReadAll(int fd,
                 void* data,
                 std::size_t size,
                 std::chrono::steady_clock::time_point deadline)
{
    auto* p               = static_cast<std::uint8_t*>(data);
    std::size_t remaining = size;

    while (remaining > 0)
    {
        const ssize_t n = ::recv(fd, p, remaining, 0);

        if (n > 0)
        {
            p         += n;
            remaining -= static_cast<std::size_t>(n);
            continue;
        }

        if (n == 0)
            return IpcError::PEER_CLOSED;

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                return IpcError::TIMEOUT;

            const auto remaining_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now);

            pollfd pfd{fd, POLLIN, 0};
            const int r = ::poll(&pfd, 1,
                                 static_cast<int>(remaining_ms.count()));

            if (r == 0)                   return IpcError::TIMEOUT;
            if (r < 0 && errno == EINTR)  continue;
            if (r < 0)                    return IpcError::RECEIVE_FAILED;
            continue;
        }

        return IpcError::RECEIVE_FAILED;
    }

    return IpcError::SUCCESS;
}

} // namespace

// ---------------------------------------------------------------------------
// IpcSocket — lifecycle
// ---------------------------------------------------------------------------

IpcSocket::IpcSocket() noexcept : fd_(-1) {}

IpcSocket::IpcSocket(int fd) noexcept : fd_(fd) {}

IpcSocket::~IpcSocket()
{
    Close();
}

IpcSocket::IpcSocket(IpcSocket&& other) noexcept
    : fd_(other.fd_)
{
    other.fd_ = -1;
}

IpcSocket& IpcSocket::operator=(IpcSocket&& other) noexcept
{
    if (this != &other)
    {
        Close();
        fd_       = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool IpcSocket::IsValid() const noexcept
{
    return fd_ >= 0;
}

int IpcSocket::Fd() const noexcept
{
    return fd_;
}

void IpcSocket::Close() noexcept
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// IpcSocket — Send / Recv
// ---------------------------------------------------------------------------

IpcError IpcSocket::Send(const void* data,
                         std::size_t size,
                         std::chrono::milliseconds timeout)
{
    if (fd_ < 0)
        return IpcError::NOT_CONNECTED;

    if (size > kIpcMaxMessageSize)
        return IpcError::MESSAGE_TOO_LARGE;

    if (size > 0 && data == nullptr)
        return IpcError::INVALID_ARGUMENT;

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    std::uint8_t header[kIpcFrameHeaderSize];
    EncodeHeader(static_cast<std::uint32_t>(size), header);

    IpcError r = WriteAll(fd_, header, kIpcFrameHeaderSize, deadline);
    if (r != IpcError::SUCCESS)
        return r;

    if (size > 0)
    {
        r = WriteAll(fd_, data, size, deadline);
        if (r != IpcError::SUCCESS)
            return r;
    }

    return IpcError::SUCCESS;
}

IpcError IpcSocket::Send(const std::vector<std::uint8_t>& payload,
                         std::chrono::milliseconds timeout)
{
    return Send(payload.data(), payload.size(), timeout);
}

IpcError IpcSocket::Send(const std::string& text,
                         std::chrono::milliseconds timeout)
{
    return Send(text.data(), text.size(), timeout);
}

IpcError IpcSocket::Recv(std::vector<std::uint8_t>& out_payload,
                         std::chrono::milliseconds timeout)
{
    if (fd_ < 0)
        return IpcError::NOT_CONNECTED;

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    std::uint8_t header[kIpcFrameHeaderSize];
    IpcError r = ReadAll(fd_, header, kIpcFrameHeaderSize, deadline);
    if (r != IpcError::SUCCESS)
        return r;

    const std::uint32_t len = DecodeHeader(header);

    if (len > kIpcMaxMessageSize)
        return IpcError::MESSAGE_TOO_LARGE;

    std::vector<std::uint8_t> buffer(len);

    if (len > 0)
    {
        r = ReadAll(fd_, buffer.data(), len, deadline);
        if (r != IpcError::SUCCESS)
            return r;
    }

    out_payload = std::move(buffer);
    return IpcError::SUCCESS;
}