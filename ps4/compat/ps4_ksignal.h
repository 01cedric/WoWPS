#pragma once
/*
 * ps4_ksignal.h - the console kernel's real signal ABI.
 *
 * Like `struct stat` (see ps4_kstat.h), the OpenOrbis toolchain's signal
 * headers mix musl/Linux definitions with the FreeBSD-lineage kernel the
 * console runs:
 *
 *   - `struct sigaltstack` is declared in Linux order (sp, flags, size); the
 *     kernel reads FreeBSD order (sp, size, flags).
 *   - SA_ONSTACK / SA_SIGINFO carry the Linux values (0x08000000 / 4); the
 *     kernel's are 0x0001 / 0x0040. SS_DISABLE is 4 in the kernel, not 2.
 *   - `struct sigaction` and `sigset_t` (4 x uint32) happen to match.
 *
 * A crash handler installed through the toolchain's signal() therefore runs
 * on the faulting thread's own stack, and a stack overflow - the classic
 * crash-with-no-marker on this platform - kills the process before the
 * handler can write a line. These helpers talk to libkernel's sigaction and
 * sigaltstack with the layouts the kernel expects.
 */
#include <stddef.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ps4KernelStack {
    void*  ss_sp;
    size_t ss_size;
    int    ss_flags;
} Ps4KernelStack;

enum {
    PS4_SA_ONSTACK   = 0x0001,
    PS4_SA_RESTART   = 0x0002,
    PS4_SA_RESETHAND = 0x0004,
    PS4_SA_NODEFER   = 0x0010,
    PS4_SA_SIGINFO   = 0x0040,
    PS4_SS_ONSTACK   = 0x0001,
    PS4_SS_DISABLE   = 0x0004
};

typedef void (*Ps4SigactionHandler)(int, siginfo_t*, void*);

/* Give the calling thread an alternate stack for signal delivery. `base`
 * must stay valid for the life of the thread. Returns 0 on success. */
static inline int ps4KernelSigaltstack(void* base, size_t size) {
    Ps4KernelStack st;
    st.ss_sp = base;
    st.ss_size = size;
    st.ss_flags = 0;
    return sigaltstack((const stack_t*)(const void*)&st, (stack_t*)0);
}

/* Install `handler` for `sig` with SA_SIGINFO | SA_ONSTACK. `extraFlags`
 * takes the PS4_SA_* values above. Returns 0 on success. */
static inline int ps4KernelSigaction(int sig, Ps4SigactionHandler handler, int extraFlags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.__sa_handler.__sa_sigaction = (void (*)(int, struct __siginfo*, void*))handler;
    sa.sa_flags = PS4_SA_SIGINFO | PS4_SA_ONSTACK | extraFlags;
    return sigaction(sig, &sa, (struct sigaction*)0);
}

#ifdef __cplusplus
}
#endif
