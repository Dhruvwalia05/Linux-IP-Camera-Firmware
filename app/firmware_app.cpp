#include "firmware_app.h"
#include "framework/logger/logger.h"
#include "framework/util/helgrind_annotations.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace
{

constexpr std::chrono::milliseconds RUN_LOOP_INTERVAL{100};

const char* StateToString(FirmwareApp::State state)
{
    switch (state)
    {
        case FirmwareApp::State::UNINITIALIZED: return "UNINITIALIZED";
        case FirmwareApp::State::INITIALIZED:   return "INITIALIZED";
        case FirmwareApp::State::RUNNING:       return "RUNNING";
        case FirmwareApp::State::SHUTDOWN:      return "SHUTDOWN";
    }
    return "UNKNOWN";
}

} // namespace

FirmwareApp::FirmwareApp()
    : shutdown_requested(false),
      state(State::UNINITIALIZED)
{
}

FirmwareApp::~FirmwareApp()
{
    const State current = state.load();

    if (current == State::RUNNING)
    {
        /*
         * The caller destroyed FirmwareApp while Run() was still
         * executing. This is a programming error — Run() blocks on
         * the calling thread and cannot be stopped from a destructor.
         * Use stderr, not Logger, because Logger may already be
         * shut down or never initialized.
         */
        std::fprintf(
            stderr,
            "[FirmwareApp] FATAL: destructor called while state=%s. "
            "Run() must return before destruction.\n",
            StateToString(current));
        return;
    }

    if (current != State::SHUTDOWN)
    {
        // Safety net: user forgot to call Shutdown(), or an exception
        // was thrown between Initialize() and Shutdown().
        Shutdown();
    }
}

bool FirmwareApp::Initialize()
{
    if (state.load() != State::UNINITIALIZED)
    {
        return false;
    }

    Logger& logger = Logger::GetInstance();

    // Logger must come up first so subsequent components can log.
    // Default config for now; a ConfigService will supply it later.
    if (logger.Initialize() != LoggerError::SUCCESS)
    {
        return false;
    }

    logger.Info("FirmwareApp: initializing");

    if (!signalHandler.Initialize())
    {
        logger.Error("FirmwareApp: SignalHandler initialization failed");
        logger.Shutdown();
        return false;
    }

    state.store(State::INITIALIZED);

    logger.Info("FirmwareApp: initialized");

    return true;
}

void FirmwareApp::Run()
{
    if (state.load() != State::INITIALIZED)
    {
        return;
    }

    state.store(State::RUNNING, std::memory_order_release);
    ANNOTATE_HAPPENS_BEFORE(&state);

    Logger& logger = Logger::GetInstance();
    logger.Info("FirmwareApp: run loop entered");

    while (!IsShutdownRequested())
    {
        std::this_thread::sleep_for(RUN_LOOP_INTERVAL);
    }

    logger.Info("FirmwareApp: run loop exited");
}

void FirmwareApp::RequestShutdown()
{
    shutdown_requested.store(true, std::memory_order_relaxed);
    ANNOTATE_HAPPENS_BEFORE(&shutdown_requested);
}

void FirmwareApp::Shutdown()
{
    // Atomically claim the shutdown transition. If another thread
    // (or a second call from the same thread) already transitioned
    // us to SHUTDOWN, that caller is responsible for cleanup.
    const State previous = state.exchange(State::SHUTDOWN);

    if (previous == State::SHUTDOWN)
    {
        // Already shut down (idempotent path).
        return;
    }

    if (previous == State::UNINITIALIZED)
    {
        // Nothing was initialized; nothing to clean up.
        return;
    }

    Logger& logger = Logger::GetInstance();

    logger.Info("FirmwareApp: shutting down");

    /*
     * Shut down subsystems in reverse order of initialization.
     *
     * Initialize order:  Logger → SignalHandler → (services...)
     * Shutdown  order:  (services...) → SignalHandler → Logger
     *
     * Logger shuts down last so we can log every other step.
     */
    signalHandler.Shutdown();

    logger.Info("FirmwareApp: shutdown complete");

    logger.Shutdown();
}

FirmwareApp::State FirmwareApp::GetState() const
{
    const State current = state.load(std::memory_order_acquire);
    ANNOTATE_HAPPENS_AFTER(&state);
    return current;
}

bool FirmwareApp::IsShutdownRequested() const
{
    if (shutdown_requested.load(std::memory_order_relaxed))
    {
        ANNOTATE_HAPPENS_AFTER(&shutdown_requested);
        return true;
    }

    return signalHandler.IsShutdownRequested();
}
