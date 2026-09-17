#include "stop_token.h"

StopToken::StopToken()
    : stop_requested(false)
{

}

void StopToken::RequestStop()
{
    stop_requested.store(true);
}

bool StopToken::IsStopRequested() const
{
    return stop_requested.load();
}

void StopToken::Reset()
{
    stop_requested.store(false);
}
