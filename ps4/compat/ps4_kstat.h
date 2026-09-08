#pragma once
/*
 * ps4_kstat.h - the console kernel's real `struct stat` layout.
 *
 * The OpenOrbis toolchain's <sys/stat.h> declares `mode_t` as a 4-byte
 * unsigned int. The PS4 kernel (FreeBSD 9 lineage) writes a 2-byte st_mode
 * followed by a 2-byte st_nlink, so everything from st_uid on is shifted in
 * the toolchain's struct: reading `st_size` through it returns the kernel's
 * `st_blocks`. Measured on hardware (B4 test 4): StormLib's fstat reported
 * 196160 for a 100,373,935-byte archive - exactly its 512-byte block count -
 * while seek-to-end reported the true size. Every archive of the client was
 * rejected as "shorter than its own hash table" because of it.
 *
 * libc++'s std::filesystem in the same toolchain reports correct sizes (its
 * library was evidently built against a correct declaration), so this only
 * bites code compiled here that calls stat/fstat/lstat directly: StormLib's
 * FileStream.cpp, miniaudio's stdio VFS, and anything else that follows.
 *
 * This header is the layout the kernel actually fills. Use it with the plain
 * stat()/fstat() symbols (which are libkernel's syscall wrappers) by passing
 * a pointer to this struct instead of the toolchain's `struct stat`.
 */
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Ps4KernelTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct Ps4KernelStat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    struct Ps4KernelTimespec st_atim;
    struct Ps4KernelTimespec st_mtim;
    struct Ps4KernelTimespec st_ctim;
    int64_t  st_size;
    int64_t  st_blocks;
    uint32_t st_blksize;
    uint32_t st_flags;
    uint32_t st_gen;
    int32_t  st_lspare;
    struct Ps4KernelTimespec st_birthtim;
};

#ifdef __cplusplus
static_assert(sizeof(struct Ps4KernelStat) == 120, "Ps4KernelStat must match the 120-byte kernel layout");
#endif

static inline int ps4KernelFstat(int fd, struct Ps4KernelStat* out) {
    return fstat(fd, (struct stat*)(void*)out);
}

static inline int ps4KernelStat(const char* path, struct Ps4KernelStat* out) {
    return stat(path, (struct stat*)(void*)out);
}

#ifdef __cplusplus
}
#endif
