#include "framework/ipc/ipc.h"
#include "framework/ipc/ipc_internal.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

int tests_run    = 0;
int tests_passed = 0;

void Check(bool condition, const char* name)
{
    ++tests_run;
    if (condition)
    {
        ++tests_passed;
        std::fprintf(stderr, "[PASS] %s\n", name);
    }
    else
    {
        std::fprintf(stderr, "[FAIL] %s\n", name);
    }
}

std::string MakeTempPath()
{
    static std::atomic<int> counter{0};
    char buf[128];
    std::snprintf(buf, sizeof(buf), "/tmp/ipc_test_%d_%d.sock",
                  static_cast<int>(::getpid()), counter.fetch_add(1));
    return std::string(buf);
}

// Build a pair of connected, non-blocking IpcSockets using socketpair().
// Returns false on failure.
bool MakeSocketPair(IpcSocket& a, IpcSocket& b)
{
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        return false;

    if (!ipc_internal::SetNonBlocking(fds[0]) ||
        !ipc_internal::SetNonBlocking(fds[1]))
    {
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }

    a = IpcSocket(fds[0]);
    b = IpcSocket(fds[1]);
    return true;
}

// ======================= IpcSocket-level tests =======================

void TestSocketPairBasicRoundTrip()
{
    IpcSocket a, b;
    Check(MakeSocketPair(a, b), "socketpair created");

    Check(a.Send("hello", 1000ms) == IpcError::SUCCESS,
          "socketpair: send hello");

    std::vector<std::uint8_t> msg;
    Check(b.Recv(msg, 1000ms) == IpcError::SUCCESS,
          "socketpair: recv message");
    Check(std::string(msg.begin(), msg.end()) == "hello",
          "socketpair: message content preserved");
}

void TestSocketPairBackToBack()
{
    IpcSocket a, b;
    MakeSocketPair(a, b);

    Check(a.Send("AAA", 1000ms) == IpcError::SUCCESS, "back-to-back: send AAA");
    Check(a.Send("BBB", 1000ms) == IpcError::SUCCESS, "back-to-back: send BBB");
    Check(a.Send("CCC", 1000ms) == IpcError::SUCCESS, "back-to-back: send CCC");

    std::vector<std::uint8_t> msg;

    Check(b.Recv(msg, 1000ms) == IpcError::SUCCESS &&
          std::string(msg.begin(), msg.end()) == "AAA",
          "back-to-back: recv AAA as distinct message");

    Check(b.Recv(msg, 1000ms) == IpcError::SUCCESS &&
          std::string(msg.begin(), msg.end()) == "BBB",
          "back-to-back: recv BBB as distinct message");

    Check(b.Recv(msg, 1000ms) == IpcError::SUCCESS &&
          std::string(msg.begin(), msg.end()) == "CCC",
          "back-to-back: recv CCC as distinct message");
}

void TestSocketPairEmptyMessage()
{
    IpcSocket a, b;
    MakeSocketPair(a, b);

    Check(a.Send("", 1000ms) == IpcError::SUCCESS,
          "empty message: send");

    std::vector<std::uint8_t> msg;
    Check(b.Recv(msg, 1000ms) == IpcError::SUCCESS,
          "empty message: recv");
    Check(msg.empty(), "empty message: payload is empty");
}

void TestSocketPairBinarySafety()
{
    IpcSocket a, b;
    MakeSocketPair(a, b);

    // All 256 byte values, including 0x00 and 0xFF
    std::vector<std::uint8_t> payload(256);
    for (int i = 0; i < 256; ++i)
        payload[i] = static_cast<std::uint8_t>(i);

    Check(a.Send(payload, 1000ms) == IpcError::SUCCESS,
          "binary: send all 256 byte values");

    std::vector<std::uint8_t> msg;
    Check(b.Recv(msg, 1000ms) == IpcError::SUCCESS,
          "binary: recv");
    Check(msg == payload, "binary: payload preserved exactly");
}

void TestSocketPairLargeMessage()
{
    IpcSocket a, b;
    MakeSocketPair(a, b);

    const std::size_t kSize = 500 * 1024;   // 500 KB
    std::vector<std::uint8_t> payload(kSize);
    for (std::size_t i = 0; i < kSize; ++i)
        payload[i] = static_cast<std::uint8_t>(i & 0xFF);

    // Send 500 KB from a separate thread. The kernel's socket buffer
    // is smaller than the payload, so Send() blocks until the reader
    // drains. A single-threaded send-then-receive test would deadlock
    // (both ends waiting on the same thread).
    IpcError send_result = IpcError::SEND_FAILED;

    std::thread sender([&]()
    {
        send_result = a.Send(payload, 10000ms);
    });

    std::vector<std::uint8_t> msg;
    const IpcError recv_result = b.Recv(msg, 10000ms);

    sender.join();

    Check(send_result == IpcError::SUCCESS, "large message: send 500 KB");
    Check(recv_result == IpcError::SUCCESS, "large message: recv");
    Check(msg == payload, "large message: payload preserved");
}

void TestSocketPairOversizedSendRejected()
{
    IpcSocket a, b;
    MakeSocketPair(a, b);

    std::vector<std::uint8_t> payload(kIpcMaxMessageSize + 1);

    Check(a.Send(payload, 1000ms) == IpcError::MESSAGE_TOO_LARGE,
          "oversized: send rejected");
}

void TestSocketPairNullDataRejected()
{
    IpcSocket a, b;
    MakeSocketPair(a, b);

    Check(a.Send(nullptr, 100, 1000ms) == IpcError::INVALID_ARGUMENT,
          "null data with size>0: rejected");
}

void TestSocketPairRecvTimeout()
{
    IpcSocket a, b;
    MakeSocketPair(a, b);

    std::vector<std::uint8_t> msg;
    const auto start = std::chrono::steady_clock::now();
    const auto result = b.Recv(msg, 150ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    Check(result == IpcError::TIMEOUT, "recv on idle socket: TIMEOUT");
    Check(elapsed >= 100ms && elapsed < 500ms,
          "recv timeout: bounded wait");
    Check(msg.empty(),
          "recv timeout: out payload untouched");
}

void TestSocketPairPeerClosed()
{
    IpcSocket a, b;
    MakeSocketPair(a, b);

    a.Close();

    std::vector<std::uint8_t> msg;
    Check(b.Recv(msg, 1000ms) == IpcError::PEER_CLOSED,
          "recv after peer closed: PEER_CLOSED");
}

void TestSocketInvalidDefault()
{
    IpcSocket socket;

    Check(!socket.IsValid(), "default socket: invalid");
    Check(socket.Send("x", 100ms) == IpcError::NOT_CONNECTED,
          "default socket: Send returns NOT_CONNECTED");

    std::vector<std::uint8_t> msg;
    Check(socket.Recv(msg, 100ms) == IpcError::NOT_CONNECTED,
          "default socket: Recv returns NOT_CONNECTED");
}

void TestSocketMoveSemantics()
{
    IpcSocket a, b;
    MakeSocketPair(a, b);

    IpcSocket moved = std::move(a);

    Check(!a.IsValid(), "moved-from socket: invalid");
    Check(moved.IsValid(), "moved-to socket: valid");

    Check(moved.Send("x", 1000ms) == IpcError::SUCCESS,
          "moved-to socket: Send works");
}

// ======================= Server / Client =======================

void TestServerInitialState()
{
    IpcServer server;
    Check(!server.IsRunning(), "server: initial not running");
    Check(!server.HasClient(), "server: initial no client");
}

void TestServerStartStop()
{
    IpcServer server;
    const std::string path = MakeTempPath();

    Check(server.Start(path) == IpcError::SUCCESS,
          "server: start succeeds");
    Check(server.IsRunning(), "server: running after start");

    server.Stop();

    Check(!server.IsRunning(), "server: stopped");
    Check(!server.HasClient(), "server: no client after stop");
}

void TestServerDoubleStart()
{
    IpcServer server;
    const std::string path = MakeTempPath();

    server.Start(path);
    Check(server.Start(path) == IpcError::ALREADY_RUNNING,
          "server: double start rejected");
    server.Stop();
}

void TestServerStartEmptyPath()
{
    IpcServer server;
    Check(server.Start("") == IpcError::INVALID_ARGUMENT,
          "server: empty path rejected");
}

void TestServerStopIdempotent()
{
    IpcServer server;
    server.Stop();
    server.Stop();
    Check(true, "server: double Stop is safe");
}

void TestServerAcceptBeforeStart()
{
    IpcServer server;
    Check(server.Accept(100ms) == IpcError::NOT_CONNECTED,
          "server: Accept before Start = NOT_CONNECTED");
}

void TestServerAcceptTimeout()
{
    IpcServer server;
    const std::string path = MakeTempPath();
    server.Start(path);

    const auto start = std::chrono::steady_clock::now();
    const auto r = server.Accept(150ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    Check(r == IpcError::TIMEOUT, "server: Accept times out with no client");
    Check(elapsed >= 100ms && elapsed < 500ms,
          "server: Accept timeout bounded");

    server.Stop();
}

void TestServerSendBeforeAccept()
{
    IpcServer server;
    const std::string path = MakeTempPath();
    server.Start(path);

    Check(server.Send("x", 100ms) == IpcError::NOT_CONNECTED,
          "server: Send before Accept = NOT_CONNECTED");

    server.Stop();
}

void TestClientInitialState()
{
    IpcClient client;
    Check(!client.IsConnected(), "client: initial not connected");
}

void TestClientConnectEmptyPath()
{
    IpcClient client;
    Check(client.Connect("") == IpcError::INVALID_ARGUMENT,
          "client: empty path rejected");
}

void TestClientConnectInvalidPath()
{
    IpcClient client;
    Check(client.Connect("/tmp/ipc_definitely_does_not_exist_xyz.sock")
          == IpcError::CONNECT_FAILED,
          "client: connect to nonexistent path = CONNECT_FAILED");
}

void TestClientSendBeforeConnect()
{
    IpcClient client;
    Check(client.Send("x", 100ms) == IpcError::NOT_CONNECTED,
          "client: Send before Connect = NOT_CONNECTED");
}

void TestBasicRoundTrip()
{
    IpcServer server;
    const std::string path = MakeTempPath();

    Check(server.Start(path) == IpcError::SUCCESS,
          "roundtrip: server started");

    std::thread server_worker([&]()
    {
        if (server.Accept(2000ms) != IpcError::SUCCESS)
            return;

        std::vector<std::uint8_t> msg;
        if (server.Recv(msg, 2000ms) != IpcError::SUCCESS)
            return;

        server.Send(msg, 2000ms);   // echo back
    });

    IpcClient client;
    Check(client.Connect(path) == IpcError::SUCCESS,
          "roundtrip: client connected");
    Check(client.IsConnected(), "roundtrip: client reports connected");

    Check(client.Send("ping", 2000ms) == IpcError::SUCCESS,
          "roundtrip: client sent ping");

    std::vector<std::uint8_t> response;
    Check(client.Recv(response, 2000ms) == IpcError::SUCCESS,
          "roundtrip: client received response");
    Check(std::string(response.begin(), response.end()) == "ping",
          "roundtrip: echoed content correct");

    server_worker.join();
    server.Stop();
}

void TestMultipleMessages()
{
    IpcServer server;
    const std::string path = MakeTempPath();
    server.Start(path);

    constexpr int kMessages = 50;

    std::thread server_worker([&]()
    {
        if (server.Accept(2000ms) != IpcError::SUCCESS)
            return;

        for (int i = 0; i < kMessages; ++i)
        {
            std::vector<std::uint8_t> msg;
            if (server.Recv(msg, 2000ms) != IpcError::SUCCESS)
                return;
            if (server.Send(msg, 2000ms) != IpcError::SUCCESS)
                return;
        }
    });

    IpcClient client;
    Check(client.Connect(path) == IpcError::SUCCESS,
          "multiple: client connected");

    bool all_ok = true;

    for (int i = 0; i < kMessages; ++i)
    {
        const std::string payload = "msg-" + std::to_string(i);

        if (client.Send(payload, 2000ms) != IpcError::SUCCESS)
        {
            all_ok = false;
            break;
        }

        std::vector<std::uint8_t> response;
        if (client.Recv(response, 2000ms) != IpcError::SUCCESS)
        {
            all_ok = false;
            break;
        }

        if (std::string(response.begin(), response.end()) != payload)
        {
            all_ok = false;
            break;
        }
    }

    Check(all_ok, "multiple: all 50 messages echoed correctly");

    server_worker.join();
    server.Stop();
}

void TestServerRejectsSecondAcceptWhileClientConnected()
{
    IpcServer server;
    const std::string path = MakeTempPath();
    server.Start(path);

    std::thread server_worker([&]()
    {
        server.Accept(2000ms);
    });

    IpcClient client;
    client.Connect(path);

    server_worker.join();

    Check(server.HasClient(), "second accept: client connected");

    // Now try to Accept again — should fail
    Check(server.Accept(100ms) == IpcError::ALREADY_CONNECTED,
          "second accept: ALREADY_CONNECTED while client connected");

    server.Disconnect();
    Check(!server.HasClient(), "second accept: client disconnected");

    server.Stop();
}

void TestConnectWithRetry()
{
    const std::string path = MakeTempPath();

    IpcClient client;

    // Server isn't running yet — first attempts will fail.
    // Start the server after a short delay in another thread.
    std::thread server_starter([&]()
    {
        std::this_thread::sleep_for(150ms);

        IpcServer server;
        server.Start(path);

        if (server.Accept(2000ms) == IpcError::SUCCESS)
        {
            std::vector<std::uint8_t> msg;
            if (server.Recv(msg, 2000ms) == IpcError::SUCCESS)
                server.Send(msg, 2000ms);
        }

        server.Stop();
    });

    const auto r = client.ConnectWithRetry(path, 10, 50ms, 200ms);

    Check(r == IpcError::SUCCESS,
          "retry: ConnectWithRetry eventually succeeds");

    Check(client.Send("hello", 2000ms) == IpcError::SUCCESS,
          "retry: send after connect");

    std::vector<std::uint8_t> response;
    Check(client.Recv(response, 2000ms) == IpcError::SUCCESS,
          "retry: recv response");

    server_starter.join();
}

// ======================= Stale socket recovery =======================

void TestStaleSocketCleanup()
{
    const std::string path = MakeTempPath();

    int sync[2];
    if (::pipe(sync) != 0)
    {
        Check(false, "stale: pipe failed");
        return;
    }

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(sync[0]);
        ::close(sync[1]);
        Check(false, "stale: fork failed");
        return;
    }

    if (pid == 0)
    {
        // ---- Child ----
        ::close(sync[0]);

        IpcServer server;
        const char status =
            (server.Start(path) == IpcError::SUCCESS) ? 'R' : 'F';
        ::write(sync[1], &status, 1);
        ::close(sync[1]);

        // Wait forever — parent will SIGKILL us
        for (;;)
            ::pause();
    }

    // ---- Parent ----
    ::close(sync[1]);

    char status = 0;
    ::read(sync[0], &status, 1);
    ::close(sync[0]);

    if (status != 'R')
    {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, nullptr, 0);
        Check(false, "stale: child server failed to start");
        return;
    }

    // Kill the child without letting it clean up
    ::kill(pid, SIGKILL);
    ::waitpid(pid, nullptr, 0);

    // Socket file should still exist. Lock was released by kernel.
    // A new server should acquire the lock, unlink the stale socket,
    // and bind successfully.
    IpcServer server2;
    const auto r = server2.Start(path);

    Check(r == IpcError::SUCCESS,
          "stale: new server recovers from stale socket file");

    if (r == IpcError::SUCCESS)
        server2.Stop();
}

} // namespace

int main()
{
    std::fprintf(stderr, "=== IPC Test Suite ===\n\n");

    // IpcSocket-level
    TestSocketPairBasicRoundTrip();
    TestSocketPairBackToBack();
    TestSocketPairEmptyMessage();
    TestSocketPairBinarySafety();
    TestSocketPairLargeMessage();
    TestSocketPairOversizedSendRejected();
    TestSocketPairNullDataRejected();
    TestSocketPairRecvTimeout();
    TestSocketPairPeerClosed();
    TestSocketInvalidDefault();
    TestSocketMoveSemantics();

    // Server / Client
    TestServerInitialState();
    TestServerStartStop();
    TestServerDoubleStart();
    TestServerStartEmptyPath();
    TestServerStopIdempotent();
    TestServerAcceptBeforeStart();
    TestServerAcceptTimeout();
    TestServerSendBeforeAccept();

    TestClientInitialState();
    TestClientConnectEmptyPath();
    TestClientConnectInvalidPath();
    TestClientSendBeforeConnect();

    TestBasicRoundTrip();
    TestMultipleMessages();
    TestServerRejectsSecondAcceptWhileClientConnected();
    TestConnectWithRetry();

    // Stale recovery
    TestStaleSocketCleanup();

    std::fprintf(stderr, "\n=== Summary ===\n");
    std::fprintf(stderr, "Tests run:    %d\n", tests_run);
    std::fprintf(stderr, "Tests passed: %d\n", tests_passed);

    if (tests_run == tests_passed)
    {
        std::fprintf(stderr, "ALL IPC TESTS PASSED\n");
        return 0;
    }

    std::fprintf(stderr, "IPC TESTS FAILED\n");
    return 1;
}