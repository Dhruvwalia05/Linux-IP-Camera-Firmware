#include "services/network/network.h"
#include "framework/event/event_bus.h"
#include "framework/logger/logger.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

// ---------------------------------------------------------------------------
// GetFreePort — ask the kernel for an unused ephemeral port.
// Race-prone in theory (something else could grab it), fine on a dev VM.
// ---------------------------------------------------------------------------
std::uint16_t GetFreePort()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        ::close(fd);
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0)
    {
        ::close(fd);
        return 0;
    }

    const std::uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

// ---------------------------------------------------------------------------
// ConnectionGrabber — subscribes to NetworkConnected and stores the
// shared_ptr<NetworkConnection> so tests can drive the server side.
// ---------------------------------------------------------------------------
struct ConnectionGrabber
{
    std::mutex                                m;
    std::condition_variable                   cv;
    std::shared_ptr<NetworkConnection>        conn;

    void OnEvent(const NetworkConnected& e)
    {
        std::lock_guard<std::mutex> lock(m);
        conn = e.connection;
        cv.notify_all();
    }

    std::shared_ptr<NetworkConnection> Wait(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, timeout, [&] { return conn != nullptr; });
        return conn;
    }
};

// ---------------------------------------------------------------------------
// EventCollector — counts NetworkDisconnected events and records the
// last reason. Used by tests that only care about event presence.
// ---------------------------------------------------------------------------
struct EventCollector
{
    std::mutex              m;
    std::condition_variable cv;
    int                     count = 0;
    NetworkError            last_reason = NetworkError::SUCCESS;

    void OnEvent(const NetworkDisconnected& e)
    {
        std::lock_guard<std::mutex> lock(m);
        ++count;
        last_reason = e.reason;
        cv.notify_all();
    }

    bool WaitFor(int target, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m);
        return cv.wait_for(lock, timeout, [&] { return count >= target; });
    }

    int Count()
    {
        std::lock_guard<std::mutex> lock(m);
        return count;
    }

    NetworkError Reason()
    {
        std::lock_guard<std::mutex> lock(m);
        return last_reason;
    }
};

// ===========================================================================
// Server lifecycle
// ===========================================================================

void TestServerInitialState()
{
    EventBus bus;
    NetworkServer server(bus);

    Check(!server.IsRunning(),          "server initial: not running");
    Check(server.ConnectionCount() == 0,"server initial: no connections");
}

void TestServerStartStop()
{
    EventBus bus;
    NetworkServer server(bus);

    const auto port = GetFreePort();
    Check(server.Start("127.0.0.1", port) == NetworkError::SUCCESS,
          "server start: succeeds");
    Check(server.IsRunning(), "server start: running");
    Check(server.ConnectionCount() == 0, "server start: zero connections");

    server.Stop();
    Check(!server.IsRunning(), "server stop: not running");
}

void TestServerDoubleStart()
{
    EventBus bus;
    NetworkServer server(bus);

    const auto port = GetFreePort();
    server.Start("127.0.0.1", port);

    Check(server.Start("127.0.0.1", port) == NetworkError::ALREADY_RUNNING,
          "server: double start rejected");

    server.Stop();
}

void TestServerEmptyAddress()
{
    EventBus bus;
    NetworkServer server(bus);

    Check(server.Start("", 8000) == NetworkError::INVALID_ARGUMENT,
          "server: empty address rejected");
}

void TestServerStopIdempotent()
{
    EventBus bus;
    NetworkServer server(bus);

    server.Stop();   // never started

    const auto port = GetFreePort();
    server.Start("127.0.0.1", port);
    server.Stop();
    server.Stop();

    Check(true, "server: stop without start / double stop safe");
}

// ===========================================================================
// Client connect
// ===========================================================================

void TestClientConnectEmptyHost()
{
    EventBus bus;
    NetworkClient client(bus);

    std::shared_ptr<NetworkConnection> conn;
    const auto r = client.Connect("", 8000, 500ms, conn);

    Check(r == NetworkError::INVALID_ARGUMENT,
          "client: empty host rejected");
    Check(conn == nullptr,
          "client: out param untouched on failure");
}

void TestClientConnectInvalidHost()
{
    EventBus bus;
    NetworkClient client(bus);

    std::shared_ptr<NetworkConnection> conn;
    const auto r = client.Connect("not.an.ip.address", 8000, 500ms, conn);

    Check(r == NetworkError::INVALID_ARGUMENT,
          "client: non-dotted-quad host rejected");
}

void TestClientConnectRefused()
{
    EventBus bus;
    NetworkClient client(bus);

    const auto port = GetFreePort();

    std::shared_ptr<NetworkConnection> conn;
    const auto r = client.Connect("127.0.0.1", port, 500ms, conn);

    Check(r == NetworkError::CONNECT_FAILED,
          "client: refused connect returns CONNECT_FAILED");
    Check(conn == nullptr,
          "client: out param untouched on refused");
}

// ===========================================================================
// Round-trip
// ===========================================================================

void TestBasicRoundTrip()
{
    EventBus bus;
    const auto port = GetFreePort();

    NetworkServer server(bus);
    ConnectionGrabber grabber;

    auto sub = bus.Subscribe<NetworkConnected>(
        [&grabber](const NetworkConnected& e) { grabber.OnEvent(e); });

    Check(server.Start("127.0.0.1", port) == NetworkError::SUCCESS,
          "round trip: server started");

    NetworkClient client(bus);
    std::shared_ptr<NetworkConnection> client_conn;

    const auto cr = client.Connect("127.0.0.1", port, 2000ms, client_conn);
    Check(cr == NetworkError::SUCCESS, "round trip: client connected");
    Check(client_conn != nullptr,       "round trip: client has connection");

    auto server_conn = grabber.Wait(2000ms);
    Check(server_conn != nullptr,       "round trip: server holds connection");
    Check(server.ConnectionCount() == 1,"round trip: server tracked connection");

    // client → server
    Check(client_conn->Send("hello", 1000ms) == NetworkError::SUCCESS,
          "round trip: client sent");

    std::vector<std::uint8_t> from_client;
    auto rr = server_conn->Recv(from_client, 1024, 2000ms);
    Check(rr == NetworkError::SUCCESS,
          "round trip: server received");
    Check(std::string(from_client.begin(), from_client.end()) == "hello",
          "round trip: server received correct bytes");

    // server → client
    Check(server_conn->Send("world", 1000ms) == NetworkError::SUCCESS,
          "round trip: server sent");

    std::vector<std::uint8_t> from_server;
    rr = client_conn->Recv(from_server, 1024, 2000ms);
    Check(rr == NetworkError::SUCCESS,
          "round trip: client received");
    Check(std::string(from_server.begin(), from_server.end()) == "world",
          "round trip: client received correct bytes");

    server.Stop();
}

void TestMultipleMessages()
{
    EventBus bus;
    const auto port = GetFreePort();

    NetworkServer server(bus);
    ConnectionGrabber grabber;

    auto sub = bus.Subscribe<NetworkConnected>(
        [&grabber](const NetworkConnected& e) { grabber.OnEvent(e); });

    server.Start("127.0.0.1", port);

    NetworkClient client(bus);
    std::shared_ptr<NetworkConnection> client_conn;
    client.Connect("127.0.0.1", port, 2000ms, client_conn);

    auto server_conn = grabber.Wait(2000ms);
    Check(server_conn != nullptr, "multi-msg: connected");

    constexpr int kMessages = 25;
    bool all_ok = true;

    for (int i = 0; i < kMessages && all_ok; ++i)
    {
        const std::string payload = "msg-" + std::to_string(i);

        if (client_conn->Send(payload, 1000ms) != NetworkError::SUCCESS) {
            all_ok = false;
            break;
        }

        std::vector<std::uint8_t> got;
        if (server_conn->Recv(got, 256, 2000ms) != NetworkError::SUCCESS) {
            all_ok = false;
            break;
        }

        if (std::string(got.begin(), got.end()) != payload) {
            all_ok = false;
            break;
        }
    }

    Check(all_ok, "multi-msg: all 25 messages delivered in order");

    server.Stop();
}

void TestLargePayloadSplit()
{
    EventBus bus;
    const auto port = GetFreePort();

    NetworkServer server(bus);
    ConnectionGrabber grabber;

    auto sub = bus.Subscribe<NetworkConnected>(
        [&grabber](const NetworkConnected& e) { grabber.OnEvent(e); });

    server.Start("127.0.0.1", port);

    NetworkClient client(bus);
    std::shared_ptr<NetworkConnection> client_conn;
    client.Connect("127.0.0.1", port, 2000ms, client_conn);

    auto server_conn = grabber.Wait(2000ms);
    Check(server_conn != nullptr, "large payload: connected");

    // 500 KB total = 2 chunks of 250 KB (each below kMaxSendChunkSize)
    const std::size_t chunk_size = 250 * 1024;
    std::vector<std::uint8_t> chunk1(chunk_size, 0xAA);
    std::vector<std::uint8_t> chunk2(chunk_size, 0xBB);

    std::thread sender([&]() {
        client_conn->Send(chunk1, 3000ms);
        client_conn->Send(chunk2, 3000ms);
    });

    std::vector<std::uint8_t> total;
    total.reserve(2 * chunk_size);

    while (total.size() < 2 * chunk_size)
    {
        std::vector<std::uint8_t> piece;
        const auto r = server_conn->Recv(piece, 64 * 1024, 3000ms);
        if (r != NetworkError::SUCCESS) break;
        total.insert(total.end(), piece.begin(), piece.end());
    }

    sender.join();

    Check(total.size() == 2 * chunk_size, "large payload: received 500 KB");
    Check(total[0] == 0xAA,               "large payload: first chunk correct");
    Check(total[chunk_size] == 0xBB,      "large payload: second chunk correct");

    server.Stop();
}

// ===========================================================================
// Multiple clients
// ===========================================================================

void TestMultipleClients()
{
    EventBus bus;
    const auto port = GetFreePort();

    NetworkServer server(bus);
    server.Start("127.0.0.1", port);

    constexpr int kClients = 4;

    NetworkClient client(bus);
    std::vector<std::shared_ptr<NetworkConnection>> conns;

    for (int i = 0; i < kClients; ++i)
    {
        std::shared_ptr<NetworkConnection> c;
        const auto r = client.Connect("127.0.0.1", port, 2000ms, c);
        if (r == NetworkError::SUCCESS) {
            conns.push_back(c);
        }
    }

    Check(static_cast<int>(conns.size()) == kClients,
          "multiple clients: all connected");

    // Give the accept loop a moment to register them.
    std::this_thread::sleep_for(200ms);

    Check(server.ConnectionCount() == static_cast<std::size_t>(kClients),
          "multiple clients: server tracks all");

    server.Stop();
}

// ===========================================================================
// Disconnect event
// ===========================================================================

void TestDisconnectEventOnClientClose()
{
    EventBus bus;
    const auto port = GetFreePort();

    NetworkServer server(bus);
    ConnectionGrabber grabber;
    EventCollector disconnect_events;

    auto c_sub = bus.Subscribe<NetworkConnected>(
        [&grabber](const NetworkConnected& e) { grabber.OnEvent(e); });
    auto d_sub = bus.Subscribe<NetworkDisconnected>(
        [&disconnect_events](const NetworkDisconnected& e)
        { disconnect_events.OnEvent(e); });

    server.Start("127.0.0.1", port);

    NetworkClient client(bus);
    std::shared_ptr<NetworkConnection> client_conn;
    client.Connect("127.0.0.1", port, 2000ms, client_conn);

    auto server_conn = grabber.Wait(2000ms);
    Check(server_conn != nullptr, "disconnect: connected");

    // Receiver thread on the server side blocks in Recv. When the client
    // closes, recv returns 0 → PEER_CLOSED → Teardown → event fires.
    std::thread receiver([&]() {
        std::vector<std::uint8_t> buf;
        server_conn->Recv(buf, 1024, 5000ms);
    });

    std::this_thread::sleep_for(100ms);   // let receiver enter Recv
    client_conn->Close();

    receiver.join();

    Check(disconnect_events.WaitFor(1, 2000ms),
          "disconnect: event fired");
    Check(disconnect_events.Reason() == NetworkError::PEER_CLOSED,
          "disconnect: reason is PEER_CLOSED");

    server.Stop();
}

void TestDisconnectEventOnServerStop()
{
    EventBus bus;
    const auto port = GetFreePort();

    NetworkServer server(bus);
    ConnectionGrabber grabber;
    EventCollector disconnect_events;

    auto c_sub = bus.Subscribe<NetworkConnected>(
        [&grabber](const NetworkConnected& e) { grabber.OnEvent(e); });
    auto d_sub = bus.Subscribe<NetworkDisconnected>(
        [&disconnect_events](const NetworkDisconnected& e)
        { disconnect_events.OnEvent(e); });

    server.Start("127.0.0.1", port);

    NetworkClient client(bus);
    std::shared_ptr<NetworkConnection> client_conn;
    client.Connect("127.0.0.1", port, 2000ms, client_conn);

    auto server_conn = grabber.Wait(2000ms);
    Check(server_conn != nullptr, "stop disconnect: connected");

    server.Stop();

    Check(disconnect_events.WaitFor(1, 2000ms),
          "stop disconnect: event fired");
    Check(disconnect_events.Reason() == NetworkError::SHUTDOWN,
          "stop disconnect: reason is SHUTDOWN");
}

// ===========================================================================
// Concurrent
// ===========================================================================

void TestConcurrentClients()
{
    EventBus bus;
    const auto port = GetFreePort();

    NetworkServer server(bus);
    server.Start("127.0.0.1", port);

    constexpr int kClients = 6;

    std::atomic<int> successful{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < kClients; ++i)
    {
        threads.emplace_back([&, i]() {
            NetworkClient client(bus);
            std::shared_ptr<NetworkConnection> c;
            const auto r = client.Connect("127.0.0.1", port, 3000ms, c);
            if (r == NetworkError::SUCCESS) {
                successful.fetch_add(1);
                // Hold the connection open for a moment.
                std::this_thread::sleep_for(200ms);
            }
        });
    }

    for (auto& t : threads) t.join();

    Check(successful.load() == kClients,
          "concurrent: all clients connected");

    std::this_thread::sleep_for(200ms);

    // Some may have disconnected by now (their connections were dropped).
    // We only assert that the server handled the burst without crashing.
    Check(server.IsRunning(),
          "concurrent: server still running");

    server.Stop();
}

void TestConcurrentSends()
{
    EventBus bus;
    const auto port = GetFreePort();

    NetworkServer server(bus);
    ConnectionGrabber grabber;

    auto sub = bus.Subscribe<NetworkConnected>(
        [&grabber](const NetworkConnected& e) { grabber.OnEvent(e); });

    server.Start("127.0.0.1", port);

    NetworkClient client(bus);
    std::shared_ptr<NetworkConnection> client_conn;
    client.Connect("127.0.0.1", port, 2000ms, client_conn);

    auto server_conn = grabber.Wait(2000ms);
    Check(server_conn != nullptr, "concurrent sends: connected");

    constexpr int kThreads        = 4;
    constexpr int kPerThread      = 10;
    constexpr int kExpectedBytes  = kThreads * kPerThread;   // 1 byte per msg

    std::atomic<int> total_bytes{0};

    // TCP is a byte stream: Recv may batch multiple sends into one call.
    // We count bytes, not Recv invocations.
    std::thread receiver([&]() {
        while (total_bytes.load() < kExpectedBytes)
        {
            std::vector<std::uint8_t> buf;
            if (server_conn->Recv(buf, 1024, 2000ms) != NetworkError::SUCCESS)
                break;
            total_bytes.fetch_add(static_cast<int>(buf.size()));
        }
    });

    std::vector<std::thread> senders;
    for (int i = 0; i < kThreads; ++i)
    {
        senders.emplace_back([&]() {
            for (int j = 0; j < kPerThread; ++j)
            {
                client_conn->Send("x", 1000ms);
            }
        });
    }

    for (auto& t : senders) t.join();
    receiver.join();

    Check(total_bytes.load() == kExpectedBytes,
          "concurrent sends: all 40 bytes received");

    server.Stop();
}

} // namespace

int main()
{
    std::fprintf(stderr, "=== Network Test Suite ===\n\n");

    Logger::GetInstance().Initialize();

    // Server lifecycle
    TestServerInitialState();
    TestServerStartStop();
    TestServerDoubleStart();
    TestServerEmptyAddress();
    TestServerStopIdempotent();

    // Client connect
    TestClientConnectEmptyHost();
    TestClientConnectInvalidHost();
    TestClientConnectRefused();

    // Round-trip
    TestBasicRoundTrip();
    TestMultipleMessages();
    TestLargePayloadSplit();

    // Multiple clients
    TestMultipleClients();

    // Disconnect events
    TestDisconnectEventOnClientClose();
    TestDisconnectEventOnServerStop();

    // Concurrency
    TestConcurrentClients();
    TestConcurrentSends();

    Logger::GetInstance().Shutdown();

    std::fprintf(stderr, "\n=== Summary ===\n");
    std::fprintf(stderr, "Tests run:    %d\n", tests_run);
    std::fprintf(stderr, "Tests passed: %d\n", tests_passed);

    if (tests_run == tests_passed)
    {
        std::fprintf(stderr, "ALL NETWORK TESTS PASSED\n");
        return 0;
    }

    std::fprintf(stderr, "NETWORK TESTS FAILED\n");
    return 1;
}