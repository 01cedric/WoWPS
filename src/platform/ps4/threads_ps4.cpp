// threads_ps4.cpp - every thread the client creates gets a real stack.
//
// The console's default thread stack is small enough that miniaudio's device
// thread overflowed it on the first MP3 decode (B4 test 7: CE-34878-0 right
// after "Music playback started", no crash marker because the handler had no
// stack to run on). std::thread, std::async and every library that calls
// pthread_create with a null attribute get that default. libc++'s
// std::thread creation is inline in its headers, so the pthread_create
// reference comes out of this executable's own objects and binds to the
// definition below rather than to libkernel's; the real one is reached
// through scePthreadCreate.
#include "platform/ps4/ps4_platform.hpp"

#include <orbis/libkernel.h>

#include <pthread.h>
#include <cstddef>
#include <cstdint>

namespace {
constexpr size_t kThreadStackBytes = 2u * 1024u * 1024u;
constexpr size_t kMinimumStackBytes = 256u * 1024u;
}

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                              void* (*start)(void*), void* arg) {
    // The toolchain's pthread_attr_t is musl's 56-byte struct; the kernel's is
    // a pointer, which libkernel stores in the first eight bytes. Large enough,
    // and the same libkernel functions read it back.
    OrbisPthreadAttr orbisAttr{};
    if (scePthreadAttrInit(&orbisAttr) != 0) {
        return scePthreadCreate(thread, nullptr, start, arg, nullptr);
    }
    size_t stackBytes = kThreadStackBytes;
    if (attr) {
        // A caller that chose a size keeps it, unless it is below what this
        // client's loaders need; the audio engine asks for 2 MiB itself.
        size_t requested = 0;
        if (pthread_attr_getstacksize(attr, &requested) == 0 && requested > 0) {
            stackBytes = requested < kMinimumStackBytes ? kMinimumStackBytes : requested;
        }
        int detach = 0;
        if (pthread_attr_getdetachstate(attr, &detach) == 0 && detach == PTHREAD_CREATE_DETACHED) {
            scePthreadAttrSetdetachstate(&orbisAttr, 1);
        }
    }
    scePthreadAttrSetstacksize(&orbisAttr, stackBytes);
    const int rc = scePthreadCreate(thread, &orbisAttr, start, arg, nullptr);
    scePthreadAttrDestroy(&orbisAttr);
    return rc;
}

namespace wowee {
namespace platform {
namespace ps4 {

size_t defaultThreadStackBytes() {
    OrbisPthreadAttr attr{};
    if (scePthreadAttrInit(&attr) != 0) return 0;
    size_t bytes = 0;
    scePthreadAttrGetstacksize(&attr, &bytes);
    scePthreadAttrDestroy(&attr);
    return bytes;
}

} // namespace ps4
} // namespace platform
} // namespace wowee
