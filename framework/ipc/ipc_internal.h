#ifndef IPC_INTERNAL_H
#define IPC_INTERNAL_H

#include <fcntl.h>

// ---------------------------------------------------------------------------
// Internal helpers shared across IPC implementation files.
//
// Not part of the public API. Callers outside framework/ipc/ should not
// include this header.
// ---------------------------------------------------------------------------

namespace ipc_internal
{

inline bool SetNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;

    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace ipc_internal

#endif // IPC_INTERNAL_H