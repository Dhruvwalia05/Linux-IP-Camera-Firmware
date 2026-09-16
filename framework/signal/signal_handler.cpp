#include "signal_handler.h"

#include <csignal>

std::atomic<bool> SignalHandler::shutdown_requested(false);

SignalHandler::SignalHandler()
{
}

SignalHandler::~SignalHandler()
{
}

void SignalHandler::SignalCallback(int signal)
{
    (void)signal;
    shutdown_requested = true;
}

bool SignalHandler::Initialize(){

    struct sigaction action;

    action.sa_handler = SignalCallback;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);

    if(sigaction(SIGINT, &action, nullptr) != 0)
        return false;

    if(sigaction(SIGTERM, &action, nullptr) != 0)
        return false;

    return true;
    
}

bool SignalHandler::IsShutdownRequested(){

    return shutdown_requested.load();

}

void SignalHandler::Shutdown(){

    shutdown_requested.store(false);

}
