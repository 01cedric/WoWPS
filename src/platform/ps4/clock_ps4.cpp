// B2: adapt the single monotonic clock requested by the supplied libc++.
// The toolchain time.h uses CLOCK_MONOTONIC=1 and libc++.a passes that value
// to the native clock_gettime import. In the hardware logs this clock only
// advanced 1ms while the realtime clock advanced 18 seconds. A native clock
// ID mismatch is inferred; the fix does not depend on guessing a BSD ID.
//
// --wrap=clock_gettime redirects references in our executable/static libraries
// here. ONLY CLOCK_MONOTONIC is intercepted; every other clock (including
// realtime and CPU clocks) retains the original native behavior and errno.
// Native pthread/condition-variable calls are not wrapped or reimplemented.
// libc++ wait_until on steady_clock converts its remaining time to a realtime
// deadline, so fixing elapsed-time reads preserves native realtime waits.

#include <cerrno>
#include <cstdint>
#include <ctime>

extern "C" uint64_t sceKernelGetProcessTime(void);
extern "C" int __real_clock_gettime(clockid_t clock, struct timespec* value);

extern "C" int __wrap_clock_gettime(clockid_t clock, struct timespec* value) {
    if (clock != CLOCK_MONOTONIC) return __real_clock_gettime(clock, value);
    if (!value) {
        errno = EFAULT;
        return -1;
    }
    const uint64_t elapsedUs = sceKernelGetProcessTime();
    value->tv_sec = static_cast<time_t>(elapsedUs / 1000000ULL);
    value->tv_nsec = static_cast<long>((elapsedUs % 1000000ULL) * 1000ULL);
    return 0;
}
