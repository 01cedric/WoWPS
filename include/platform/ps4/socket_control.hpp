#pragma once

#include <fcntl.h>

namespace wowee::platform::ps4 {

// OpenOrbis socket() returns a kernel descriptor, not a sceNet socket ID.
// Its libc forwards fcntl to _fcntl with BSD flags. FIONBIO through ioctl
// is rejected on supported console runtimes, even with the BSD request.
#ifdef WOWEE_PS4
static_assert(O_NONBLOCK == 0x0004, "TCP requires the OpenOrbis BSD fcntl ABI");
#endif

inline bool setSocketNonBlocking(int socket) {
    const int flags = ::fcntl(socket, F_GETFL, 0);
    return flags != -1 && ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) != -1;
}

} // namespace wowee::platform::ps4
