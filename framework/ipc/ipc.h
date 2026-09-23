#ifndef IPC_H
#define IPC_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ===========================================================================
// Linux IP Camera Firmware — IPC
// ===========================================================================
//
// Single-machine message-passing layer over Unix domain sockets.
//
// Design summary (see commit message for full rationale):
//
//   * Unix domain sockets, SOCK_STREAM.
//   * Length-prefixed framing: 4-byte big-endian header, then payload.
//   * Bounded messages: kIpcMaxMessageSize = 1 MB.
//   * Timed blocking API. Send/Recv take a timeout; neither can hang
//     indefinitely.
//   * Caller-supplied socket path. Production uses /run/<app>/;
//     tests use /tmp/.
//   * Single-instance server via flock on a sibling lock file.
//     Stale socket files (from crash) are detected via probe-connect
//     and unlinked automatically.
//   * Single-client server. Multiple clients would require a
//     multi-client design (Phase 3+); the current API does not
//     pretend to support it.
//
// Not supported (deliberately):
//   * Cross-machine networking (see Network service, Phase 3)
//   * TLS or any transport encryption
//   * Authentication beyond filesystem permissions
//   * Asynchronous I/O
//   * Automatic reconnect (a ConnectWithRetry convenience is provided)
//
// Threading:
//   All classes are single-threaded. One thread owns a Server or
//   Client and performs all operations on it. Concurrent use from
//   multiple threads is undefined.
//

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

enum class IpcError
{
    SUCCESS,
    INVALID_ARGUMENT,
    NOT_CONNECTED,
    ALREADY_RUNNING,
    ALREADY_CONNECTED,
    SOCKET_CREATION_FAILED,
    BIND_FAILED,
    LISTEN_FAILED,
    CONNECT_FAILED,
    SEND_FAILED,
    RECEIVE_FAILED,
    TIMEOUT,
    PEER_CLOSED,
    MESSAGE_TOO_LARGE
};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr std::size_t kIpcMaxMessageSize   = 1024 * 1024;  // 1 MB
constexpr std::size_t kIpcFrameHeaderSize  = 4;            // bytes

// ---------------------------------------------------------------------------
// IpcSocket
// ---------------------------------------------------------------------------
//
// RAII wrapper around a connected socket file descriptor, with framed
// Send / Recv. Takes ownership of the fd passed to the constructor.
//
// Move-only. The default-constructed socket is invalid.
//
// Send and Recv are framed: a message written by Send is delivered
// as exactly one message by Recv on the peer. Partial reads and
// partial writes are handled internally.
//
// Both Send and Recv honour a caller-supplied timeout. Under the hood
// the socket is non-blocking; poll() with a deadline is used to wait
// for readiness. The timeout is a wall-clock budget for the whole
// operation, not a per-syscall budget.

class IpcSocket
{
public:
    IpcSocket() noexcept;
    explicit IpcSocket(int fd) noexcept;
    ~IpcSocket();

    IpcSocket(const IpcSocket&)            = delete;
    IpcSocket& operator=(const IpcSocket&) = delete;

    IpcSocket(IpcSocket&& other) noexcept;
    IpcSocket& operator=(IpcSocket&& other) noexcept;

    bool IsValid() const noexcept;
    int  Fd() const noexcept;

    // -- Send -----------------------------------------------------------
    //
    // Encodes a 4-byte big-endian length header followed by the raw
    // payload bytes, and writes both to the socket.
    //
    // Returns:
    //   SUCCESS             — all bytes sent
    //   TIMEOUT             — timeout elapsed before completion
    //   PEER_CLOSED         — peer closed the connection mid-send
    //   SEND_FAILED         — other OS-level send error
    //   INVALID_ARGUMENT    — size > kIpcMaxMessageSize
    //   NOT_CONNECTED       — invalid socket

    IpcError Send(const void* data,
                  std::size_t size,
                  std::chrono::milliseconds timeout);

    IpcError Send(const std::vector<std::uint8_t>& payload,
                  std::chrono::milliseconds timeout);

    IpcError Send(const std::string& text,
                  std::chrono::milliseconds timeout);

    // -- Recv -----------------------------------------------------------
    //
    // Reads one framed message from the socket. On success, replaces
    // the contents of out_payload with the received bytes.
    //
    // On any non-SUCCESS return, out_payload is left unmodified.
    //
    // Returns:
    //   SUCCESS             — one message received
    //   TIMEOUT             — timeout elapsed before a full frame
    //   PEER_CLOSED         — peer closed the connection
    //   PROTOCOL_ERROR      — header decoded to length > kIpcMaxMessageSize
    //   RECEIVE_FAILED      — other OS-level receive error
    //   NOT_CONNECTED       — invalid socket

    IpcError Recv(std::vector<std::uint8_t>& out_payload,
                  std::chrono::milliseconds timeout);

    void Close() noexcept;

private:
    int fd_ = -1;
};

// ---------------------------------------------------------------------------
// IpcServer
// ---------------------------------------------------------------------------
//
// Single-instance, single-client Unix domain socket server.
//
// Start() creates the socket, acquires an exclusive flock on a
// sibling lock file (<path>.lock), and unlinks any stale socket
// file left by a crashed predecessor.
//
// After Start(), call Accept(timeout) to wait for a client. Once a
// client is accepted, Send / Recv operate on that connection.
// Disconnect() drops the current client and returns the server to
// the "listening, no client" state. Stop() tears everything down.
//
// Not movable or copyable. Owns file descriptors and a filesystem
// resource; moving would be a footgun.

class IpcServer
{
public:
    IpcServer();
    ~IpcServer();

    IpcServer(const IpcServer&)            = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    IpcServer(IpcServer&&)                 = delete;
    IpcServer& operator=(IpcServer&&)      = delete;

    // -- Lifecycle ------------------------------------------------------

    // Returns:
    //   SUCCESS             — bound, listening, lock held
    //   ALREADY_RUNNING     — another server holds the lock on this path
    //   INVALID_ARGUMENT    — socket_path is empty, or parent dir missing
    //   SOCKET_CREATION_FAILED / BIND_FAILED / LISTEN_FAILED

    IpcError Start(const std::string& socket_path);

    // Wait up to `timeout` for a client to connect.
    //
    // Returns:
    //   SUCCESS             — client accepted
    //   TIMEOUT             — no client connected in time
    //   ALREADY_CONNECTED   — a client is already accepted
    //   NOT_CONNECTED       — server not started
    //   RECEIVE_FAILED      — accept failed at the OS level

    IpcError Accept(std::chrono::milliseconds timeout);

    // -- I/O (only valid after a successful Accept) ---------------------

    IpcError Send(const void* data,
                  std::size_t size,
                  std::chrono::milliseconds timeout);

    IpcError Send(const std::vector<std::uint8_t>& payload,
                  std::chrono::milliseconds timeout);

    IpcError Send(const std::string& text,
                  std::chrono::milliseconds timeout);

    IpcError Recv(std::vector<std::uint8_t>& out_payload,
                  std::chrono::milliseconds timeout);

    // -- State ----------------------------------------------------------

    // Drop the current client connection (if any). Listening socket
    // remains open. Safe to call when no client is connected.
    void Disconnect() noexcept;

    bool IsRunning() const noexcept;
    bool HasClient() const noexcept;

    // Close the listening socket, disconnect any client, unlink the
    // socket file, release the lock. Idempotent.
    void Stop() noexcept;

private:
    int         listen_fd_ = -1;
    int         lock_fd_   = -1;
    std::string socket_path_;
    IpcSocket   client_;
    bool        running_   = false;
};

// ---------------------------------------------------------------------------
// IpcClient
// ---------------------------------------------------------------------------
//
// Single-connection Unix domain socket client.
//
// Connect() performs exactly one connect attempt. ConnectWithRetry()
// performs the same operation with exponential backoff up to a bounded
// number of attempts, for services that want a retry convenience.
//
// Not movable or copyable.

class IpcClient
{
public:
    IpcClient();
    ~IpcClient();

    IpcClient(const IpcClient&)            = delete;
    IpcClient& operator=(const IpcClient&) = delete;
    IpcClient(IpcClient&&)                 = delete;
    IpcClient& operator=(IpcClient&&)      = delete;

    // -- Lifecycle ------------------------------------------------------

    // One connect attempt.
    // Returns:
    //   SUCCESS             — connected
    //   ALREADY_CONNECTED   — Connect called on an already-connected client
    //   CONNECT_FAILED      — OS-level connect error (ENOENT, ECONNREFUSED, ...)
    //   SOCKET_CREATION_FAILED
    //   INVALID_ARGUMENT    — socket_path empty

    IpcError Connect(const std::string& socket_path);

    // Repeated Connect attempts with exponential backoff.
    // Returns SUCCESS on first success; if all attempts fail, returns
    // the error from the last attempt.

    IpcError ConnectWithRetry(const std::string& socket_path,
                              int max_attempts,
                              std::chrono::milliseconds initial_delay,
                              std::chrono::milliseconds max_delay);

    // -- I/O ------------------------------------------------------------

    IpcError Send(const void* data,
                  std::size_t size,
                  std::chrono::milliseconds timeout);

    IpcError Send(const std::vector<std::uint8_t>& payload,
                  std::chrono::milliseconds timeout);

    IpcError Send(const std::string& text,
                  std::chrono::milliseconds timeout);

    IpcError Recv(std::vector<std::uint8_t>& out_payload,
                  std::chrono::milliseconds timeout);

    // -- State ----------------------------------------------------------

    void Disconnect() noexcept;
    bool IsConnected() const noexcept;

private:
    IpcSocket socket_;
};

#endif // IPC_H