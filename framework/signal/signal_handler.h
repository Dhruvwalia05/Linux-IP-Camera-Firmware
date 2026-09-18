#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <csignal>

class SignalHandler
{
public:
    SignalHandler();
    ~SignalHandler();

    SignalHandler(const SignalHandler&)            = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;
    SignalHandler(SignalHandler&&)                 = delete;
    SignalHandler& operator=(SignalHandler&&)      = delete;

    bool Initialize();
    bool IsShutdownRequested() const;
    bool IsInstalled() const;
    void Shutdown();

private:
    enum class State { NOT_INSTALLED, INSTALLED };

    static void SignalCallback(int signal);

    static volatile sig_atomic_t shutdown_requested;

    State state;

    struct sigaction old_sigint;
    struct sigaction old_sigterm;
};

#endif // SIGNAL_HANDLER_H