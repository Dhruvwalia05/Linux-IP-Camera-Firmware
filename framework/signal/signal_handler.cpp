#include "signal_handler.h"
#include "framework/util/helgrind_annotations.h"

#include <csignal>

volatile sig_atomic_t SignalHandler::shutdown_requested = 0;

SignalHandler::SignalHandler()
    : state(State::NOT_INSTALLED)
{
    old_sigint.sa_handler  = SIG_DFL;
    old_sigterm.sa_handler = SIG_DFL;
}

SignalHandler::~SignalHandler()
{
    if (state == State::INSTALLED)
    {
        Shutdown();
    }
}

void SignalHandler::SignalCallback(int signal)
{
    (void)signal;
    shutdown_requested = 1;

    /*
     * Annotations are not formally async-signal-safe per POSIX, but
     * Valgrind's Helgrind client requests are benign in this context
     * (they write to a Valgrind-internal structure, not to user
     * memory). They are also inert in production builds.
     */
    ANNOTATE_HAPPENS_BEFORE((void*)&shutdown_requested);
}

bool SignalHandler::Initialize()
{
    if (state == State::INSTALLED)
        return false;

    shutdown_requested = 0;
    ANNOTATE_HAPPENS_BEFORE((void*)&shutdown_requested);

    struct sigaction action;
    action.sa_handler = SignalCallback;
    action.sa_flags   = 0;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, &old_sigint) != 0)
        return false;

    if (sigaction(SIGTERM, &action, &old_sigterm) != 0)
    {
        // Roll back SIGINT to leave state consistent.
        sigaction(SIGINT, &old_sigint, nullptr);
        return false;
    }

    state = State::INSTALLED;
    return true;
}

bool SignalHandler::IsShutdownRequested() const
{
    const int value = shutdown_requested;
    ANNOTATE_HAPPENS_AFTER((void*)&shutdown_requested);
    return value != 0;
}

bool SignalHandler::IsInstalled() const
{
    return state == State::INSTALLED;
}

void SignalHandler::Shutdown()
{
    if (state != State::INSTALLED)
        return;

    sigaction(SIGINT,  &old_sigint,  nullptr);
    sigaction(SIGTERM, &old_sigterm, nullptr);

    shutdown_requested = 0;
    ANNOTATE_HAPPENS_BEFORE((void*)&shutdown_requested);

    state = State::NOT_INSTALLED;
}