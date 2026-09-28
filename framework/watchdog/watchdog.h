#ifndef WATCHDOG_H
#define WATCHDOG_H

#include "framework/thread/thread_manager.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// ===========================================================================
// Watchdog
// ===========================================================================
//
// Health-aware hardware watchdog integration.
//
// Design summary:
//
//   * Device access is abstracted behind IWatchdogDevice so tests can
//     run without /dev/watchdog present. LinuxWatchdogDevice talks to
//     the real device; NullWatchdogDevice records calls for assertions.
//
//   * The Watchdog does NOT take corrective action on unhealthy checks.
//     Its sole response to failure is to STOP kicking. The hardware
//     timer then fires and the system reboots. Restart logic belongs
//     to a Supervisor (Phase 6) and would break layering if added here.
//
//   * Services register named health checkers before Start(). The list
//     is frozen at Start time — no dynamic registration while running.
//     This matches real firmware usage and eliminates a class of
//     concurrent-mutation bugs.
//
//   * The monitor thread kicks at half the hardware timeout. Kicking at
//     exactly the timeout guarantees eventual reboot under scheduler
//     jitter. T/2 provides margin.
//
//   * On clean shutdown, the device is closed with the magic 'V'
//     handshake so the kernel does not fire the watchdog after the
//     process has exited.
//
// Not supported (deliberately):
//   * Restarting unhealthy services
//   * Watchdog pretimeout (WDIOC_SETPRETIMEOUT) — not implemented on all
//     hardware; WDIOC_SETTIMEOUT is universally available
//   * Boot-status reporting (WDIOC_GETBOOTSTATUS) — diagnostic concern
//     for Phase 6
//

// ---------------------------------------------------------------------------
// IWatchdogDevice
// ---------------------------------------------------------------------------
//
// Abstract hardware watchdog. Implementations:
//   LinuxWatchdogDevice  — real /dev/watchdog
//   NullWatchdogDevice   — test double

class IWatchdogDevice
{
public:
    virtual ~IWatchdogDevice() = default;

    // Open the device and set the hardware timeout. On a real device,
    // opening the watchdog STARTS the hardware timer — you cannot open
    // it in a passive state.
    virtual bool Open(int timeout_seconds) = 0;

    // Reset the hardware timer.
    virtual bool Kick() = 0;

    // Close the device. Implementations MUST write the magic 'V'
    // character before close() so the kernel does not keep the timer
    // running after the process exits.
    virtual void Close() = 0;

    virtual bool IsOpen() const = 0;
};

// ---------------------------------------------------------------------------
// LinuxWatchdogDevice — real /dev/watchdog
// ---------------------------------------------------------------------------

class LinuxWatchdogDevice : public IWatchdogDevice
{
public:
    LinuxWatchdogDevice();
    explicit LinuxWatchdogDevice(const std::string& device_path);
    ~LinuxWatchdogDevice() override;

    LinuxWatchdogDevice(const LinuxWatchdogDevice&)            = delete;
    LinuxWatchdogDevice& operator=(const LinuxWatchdogDevice&) = delete;
    LinuxWatchdogDevice(LinuxWatchdogDevice&&)                 = delete;
    LinuxWatchdogDevice& operator=(LinuxWatchdogDevice&&)      = delete;

    bool Open(int timeout_seconds) override;
    bool Kick() override;
    void Close() override;
    bool IsOpen() const override;

private:
    std::string device_path_;
    int         fd_ = -1;
};

// ---------------------------------------------------------------------------
// NullWatchdogDevice — test double
// ---------------------------------------------------------------------------
//
// Records operations for assertions. Never touches real hardware.
// Thread-safe (uses a mutex internally) because the monitor thread
// and the test thread may both interact with it.

class NullWatchdogDevice : public IWatchdogDevice
{
public:
    NullWatchdogDevice();
    ~NullWatchdogDevice() override;

    NullWatchdogDevice(const NullWatchdogDevice&)            = delete;
    NullWatchdogDevice& operator=(const NullWatchdogDevice&) = delete;

    bool Open(int timeout_seconds) override;
    bool Kick() override;
    void Close() override;
    bool IsOpen() const override;

    int  KickCount() const;
    int  TimeoutSeconds() const;
    bool WasClosed() const;

private:
    mutable std::mutex mutex_;
    bool               open_           = false;
    int                timeout_seconds_ = 0;
    int                kicks_          = 0;
    bool               closed_         = false;
};

// ---------------------------------------------------------------------------
// Watchdog
// ---------------------------------------------------------------------------
//
// Orchestrates a device and a set of health checkers via a monitor
// thread. Composes ThreadManager for lifecycle.
//
// Lifecycle:
//
//     RegisterChecker() * N      (before Start)
//     Start(timeout_seconds)     opens device, starts monitor thread
//     Stop()                     requests monitor exit; does NOT close
//     Join()                     waits for monitor exit, closes device
//     (destructor calls Stop+Join if still running)
//
// Thread safety:
//   RegisterChecker, Start, Stop, Join are single-threaded — the
//   owner (FirmwareApp) calls them from one thread.
//   CheckerCount and IsRunning are safe to call from any thread.

class Watchdog
{
public:
    using HealthChecker = std::function<bool()>;

    Watchdog();
    explicit Watchdog(std::unique_ptr<IWatchdogDevice> device);
    ~Watchdog();

    Watchdog(const Watchdog&)            = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    Watchdog(Watchdog&&)                 = delete;
    Watchdog& operator=(Watchdog&&)      = delete;

    // Register a named health checker. Returns false if a checker with
    // the same name already exists, or if called after Start.
    bool RegisterChecker(const std::string& name, HealthChecker checker);

    // Open the device and start the monitor thread.
    // Returns false if the device cannot be opened, if already running,
    // if timeout_seconds <= 0, or if there are no registered checkers.
    bool Start(int timeout_seconds);

    // Request monitor shutdown. Does not close the device. Follow with
    // Join() for a full shutdown.
    void Stop();

    // Wait for the monitor thread to exit, then close the device with
    // the disable handshake. Idempotent.
    void Join();

    bool        IsRunning()    const;
    std::size_t CheckerCount() const;

private:
    struct Checker
    {
        std::string   name;
        HealthChecker fn;
    };

    void MonitorLoop(StopToken& token);
    bool CheckAllHealth();

    std::unique_ptr<IWatchdogDevice> device_;
    std::vector<Checker>             checkers_;
    ThreadManager                    manager_;
    int                              timeout_seconds_ = 0;
    std::atomic<bool>                running_{false};
};

#endif // WATCHDOG_H