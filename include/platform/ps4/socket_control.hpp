#pragma once

#include <sys/ioctl.h>

namespace wowee::platform::ps4 {

// OpenOrbis socket() returns a kernel socket descriptor (__sys_socketex),
// not a sceNet socket ID. Control that same descriptor through the BSD
// socket ioctl. The supplied sys/ioctl.h exposes LINUX_FIONBIO only; passing
// that Linux request (0x5421) to the PS4 kernel is not valid.
// BSD FIONBIO is _IOW('f', 126, int), including its four-byte input payload.
inline constexpr unsigned long SocketNonBlockingRequest = _IOW('f', 126, int);
static_assert(sizeof(int) == 4 && SocketNonBlockingRequest == 0x8004667eUL);

inline bool setSocketNonBlocking(int socket) {
    int enabled = 1;
    return ::ioctl(socket, SocketNonBlockingRequest, &enabled) == 0;
}

} // namespace wowee::platform::ps4
