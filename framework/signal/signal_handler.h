#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <atomic>

class SignalHandler
{
    static std::atomic<bool> shutdown_requested;
    
    static void SignalCallback(int signal);

public:
    SignalHandler();
    ~SignalHandler();

    bool Initialize();
    bool IsShutdownRequested();
    void Shutdown();
};

#endif // SIGNAL_HANDLER_H