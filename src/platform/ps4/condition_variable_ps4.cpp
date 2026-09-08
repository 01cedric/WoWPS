// OpenOrbis ships libc++ 11 with a Linux ETIMEDOUT (110) baked into
// condition_variable::__do_timed_wait. libkernel returns BSD ETIMEDOUT (60).
// Replace only that C++ ABI entry point; POSIX callers keep native error codes.
// The linker wrap is intentionally version-specific and checked by B12 tests.
#include <condition_variable>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <pthread.h>

using WaitDeadline = std::chrono::time_point<std::chrono::system_clock,
                                             std::chrono::nanoseconds>;

extern "C" void woweePs4ConditionTimedWait(std::condition_variable*,
    std::unique_lock<std::mutex>&, WaitDeadline) noexcept
    asm("__wrap__ZNSt3__118condition_variable15__do_timed_waitERNS_11unique_lockINS_5mutexEEENS_6chrono10time_pointINS5_12system_clockENS5_8durationIxNS_5ratioILl1ELl1000000000EEEEEEE");

extern "C" void woweePs4NativeTimedWait(pthread_cond_t* condition,
    pthread_mutex_t* mutex, long long count) noexcept {
#ifdef WOWEE_PS4
    static_assert(_LIBCPP_VERSION == 11000 && ETIMEDOUT == 60,
                  "Revalidate the timed-wait adapter for this SDK");
#endif
    timespec nativeDeadline{};
    nativeDeadline.tv_sec = count / 1000000000LL;
    nativeDeadline.tv_nsec = count % 1000000000LL;
    if (nativeDeadline.tv_nsec < 0) {
        --nativeDeadline.tv_sec;
        nativeDeadline.tv_nsec += 1000000000L;
    }
    const int result = pthread_cond_timedwait(condition, mutex, &nativeDeadline);
    if (result != 0 && result != ETIMEDOUT) {
        // Keep genuine mutex/clock failures fatal, with the actual native code.
        std::fprintf(stderr, "[wow_ps] condition timed wait failed: native error=%d\n", result);
        std::terminate();
    }
}

extern "C" void woweePs4ConditionTimedWait(std::condition_variable* condition,
    std::unique_lock<std::mutex>& lock, WaitDeadline deadline) noexcept {
    if (!lock.owns_lock()) std::terminate();
    woweePs4NativeTimedWait(condition->native_handle(), lock.mutex()->native_handle(),
                          deadline.time_since_epoch().count());
}
