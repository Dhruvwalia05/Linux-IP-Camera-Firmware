#ifndef FIRMWARE_APP_H
#define FIRMWARE_APP_H

#include "framework/signal/signal_handler.h"

#include <atomic>
#include <cstdint>

class FirmwareApp
{
public:
    enum class State : std::uint8_t
    {
        UNINITIALIZED,
        INITIALIZED,
        RUNNING,
        SHUTDOWN
    };

    FirmwareApp();
    ~FirmwareApp();

    FirmwareApp(const FirmwareApp&)            = delete;
    FirmwareApp& operator=(const FirmwareApp&) = delete;
    FirmwareApp(FirmwareApp&&)                 = delete;
    FirmwareApp& operator=(FirmwareApp&&)      = delete;

    bool Initialize();
    void Run();

    /*
     * Requests a shutdown of the run loop. Thread-safe and
     * async-signal-safe: it only performs a relaxed atomic store.
     * The run loop observes this and returns from Run().
     */
    void RequestShutdown();

    /*
     * Performs clean shutdown of subsystems in reverse order of
     * initialization. Idempotent: safe to call multiple times.
     */
    void Shutdown();

    State GetState() const;

private:
    bool IsShutdownRequested() const;

    SignalHandler     signalHandler;
    std::atomic<bool> shutdown_requested;
    std::atomic<State> state;
};

#endif // FIRMWARE_APP_H