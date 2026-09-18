#include "stop_token.h"

#include "framework/util/helgrind_annotations.h"

StopToken::StopToken()
    : stop_requested(false)
{
}

void StopToken::RequestStop()
{
    stop_requested.store(true, std::memory_order_release);
    ANNOTATE_HAPPENS_BEFORE(&stop_requested);
}

bool StopToken::IsStopRequested() const
{
    const bool requested =
        stop_requested.load(std::memory_order_acquire);

    if (requested)
    {
        ANNOTATE_HAPPENS_AFTER(&stop_requested);
    }

    return requested;
}

void StopToken::Reset()
{
    stop_requested.store(false, std::memory_order_release);
}