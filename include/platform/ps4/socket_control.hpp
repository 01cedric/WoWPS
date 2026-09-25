#pragma once

#include <sys/socket.h>
#ifndef WOWEE_PS4
#include <fcntl.h>
#endif

namespace wowee::platform::ps4 {

// OpenOrbis socket() returns a kernel descriptor, not a sceNet socket ID.
// On the reported PS4 runtime both fcntl and ioctl reject socket mode changes
// with EACCES. Use the PlayStation socket option instead. SO_NBIO is absent
// from the supplied musl headers; keep its ABI value local to this helper.
inline constexpr int kSocketOptionLevel = 0xffff;
inline constexpr int kSocketNonBlockingOption = 0x1200;

inline bool setSocketNonBlocking(int socket) {
#ifdef WOWEE_PS4
    const int enabled = 1;
    return ::setsockopt(socket, kSocketOptionLevel, kSocketNonBlockingOption,
                        &enabled, sizeof(enabled)) == 0;
#else
    const int flags = ::fcntl(socket, F_GETFL, 0);
    return flags != -1 && ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

} // namespace wowee::platform::ps4
