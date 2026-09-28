#include "watchdog.h"
#include "framework/logger/logger.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <unistd.h>

namespace
{

constexpr char kMagicCloseChar = 'V';
constexpr std::chrono::milliseconds kMonitorPollInterval{50};

} // namespace

// ===========================================================================
// LinuxWatchdogDevice
// ===========================================================================

LinuxWatchdogDevice::LinuxWatchdogDevice()
    : device_path_("/dev/watchdog")
{
}

LinuxWatchdogDevice::LinuxWatchdogDevice(const std::string& device_path)
    : device_path_(device_path)
{
}

LinuxWatchdogDevice::~LinuxWatchdogDevice()
{
    Close();
}

bool LinuxWatchdogDevice::Open(int timeout_seconds)
{
    if (fd_ >= 0)
        return false;

    if (timeout_seconds <= 0)
        return false;

    fd_ = ::open(device_path_.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd_ < 0)
        return false;

    int timeout = timeout_seconds;
    if (::ioctl(fd_, WDIOC_SETTIMEOUT, &timeout) < 0)
    {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    return true;
}

bool LinuxWatchdogDevice::Kick()
{
    if (fd_ < 0)
        return false;

    int dummy = 0;
    return ::ioctl(fd_, WDIOC_KEEPALIVE, &dummy) == 0;
}

void LinuxWatchdogDevice::Close()
{
    if (fd_ < 0)
        return;

    // Write the magic close character so the kernel does not keep the
    // hardware timer running after we exit. Ignore errors — a failing
    // write should not prevent close.
    ssize_t r = ::write(fd_, &kMagicCloseChar, 1);
    (void)r;

    ::close(fd_);
    fd_ = -1;
}

bool LinuxWatchdogDevice::IsOpen() const
{
    return fd_ >= 0;
}

// ===========================================================================
// NullWatchdogDevice
// ===========================================================================

NullWatchdogDevice::NullWatchdogDevice() = default;
NullWatchdogDevice::~NullWatchdogDevice() = default;

bool NullWatchdogDevice::Open(int timeout_seconds)
{
    if (timeout_seconds <= 0)
        return false;

    std::lock_guard<std::mutex> lock(mutex_);
    open_            = true;
    timeout_seconds_ = timeout_seconds;
    return true;
}

bool NullWatchdogDevice::Kick()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_)
        return false;
    ++kicks_;
    return true;
}

void NullWatchdogDevice::Close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    open_   = false;
    closed_ = true;
}

bool NullWatchdogDevice::IsOpen() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
}

int NullWatchdogDevice::KickCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return kicks_;
}

int NullWatchdogDevice::TimeoutSeconds() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return timeout_seconds_;
}

bool NullWatchdogDevice::WasClosed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
}

// ===========================================================================
// Watchdog
// ===========================================================================

Watchdog::Watchdog()
    : device_(std::make_unique<LinuxWatchdogDevice>())
{
}

Watchdog::Watchdog(std::unique_ptr<IWatchdogDevice> device)
    : device_(std::move(device))
{
}

Watchdog::~Watchdog()
{
    // Full shutdown in the destructor. Both are safe when not running.
    Stop();
    Join();
}

bool Watchdog::RegisterChecker(const std::string& name,
                               HealthChecker      checker)
{
    if (running_.load())
        return false;

    if (name.empty() || !checker)
        return false;

    for (const auto& existing : checkers_)
    {
        if (existing.name == name)
            return false;
    }

    Checker c;
    c.name = name;
    c.fn   = std::move(checker);
    checkers_.push_back(std::move(c));

    return true;
}

bool Watchdog::Start(int timeout_seconds)
{
    if (running_.load())
        return false;

    if (timeout_seconds <= 0)
        return false;

    if (checkers_.empty())
        return false;

    if (!device_)
        return false;

    if (!device_->Open(timeout_seconds))
        return false;

    timeout_seconds_ = timeout_seconds;
    running_.store(true);

    const ThreadError result = manager_.Start(
        "watchdog",
        [this](StopToken& token) { MonitorLoop(token); });

    if (result != ThreadError::SUCCESS)
    {
        device_->Close();
        running_.store(false);
        return false;
    }

    return true;
}

void Watchdog::Stop()
{
    if (!running_.load())
        return;

    manager_.Stop();
}

void Watchdog::Join()
{
    // Ensure the monitor thread will exit, then wait. Both calls are
    // safe to invoke when the manager is not running.
    manager_.Stop();
    manager_.Join();

    if (device_ && device_->IsOpen())
        device_->Close();

    running_.store(false);
}

bool Watchdog::IsRunning() const
{
    return running_.load();
}

std::size_t Watchdog::CheckerCount() const
{
    return checkers_.size();
}

void Watchdog::MonitorLoop(StopToken& token)
{
    using namespace std::chrono;

    // Kick at half the hardware timeout. A kick at exactly the timeout
    // leaves no margin for scheduler jitter; a delay of even 100 ms
    // would fire the hardware timer while the system is healthy.
    const int interval_sec = std::max(1, timeout_seconds_ / 2);
    const auto kick_interval = seconds(interval_sec);

    auto next_kick = steady_clock::now() + kick_interval;

    while (!token.IsStopRequested())
    {
        const auto now = steady_clock::now();

        if (now >= next_kick)
        {
            if (CheckAllHealth())
            {
                device_->Kick();
            }
            // else: skip the kick — the hardware will eventually fire.
            // The unhealthy checker was logged inside CheckAllHealth.

            next_kick = now + kick_interval;
        }

        std::this_thread::sleep_for(kMonitorPollInterval);
    }
}

bool Watchdog::CheckAllHealth()
{
    if (checkers_.empty())
        return false;

    Logger& logger = Logger::GetInstance();

    for (const auto& checker : checkers_)
    {
        bool healthy = false;

        try
        {
            healthy = checker.fn();
        }
        catch (const std::exception& e)
        {
            logger.Error("Watchdog: checker '" + checker.name +
                         "' threw: " + e.what());
            return false;
        }
        catch (...)
        {
            logger.Error("Watchdog: checker '" + checker.name +
                         "' threw unknown exception");
            return false;
        }

        if (!healthy)
        {
            logger.Error("Watchdog: checker '" + checker.name +
                         "' reports unhealthy — skipping kick");
            return false;
        }
    }

    return true;
}