#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#define ORBIS_KERNEL_WB_ONION 0
#define ORBIS_KERNEL_PROT_CPU_RW 3
int32_t sceKernelAllocateDirectMemory(off_t, off_t, size_t, size_t, int32_t, off_t*);
size_t sceKernelGetDirectMemorySize();
int32_t sceKernelMapDirectMemory(void**, size_t, int32_t, int32_t, off_t, size_t);
int32_t sceKernelMunmap(void*, size_t);
int32_t sceKernelReleaseDirectMemory(off_t, size_t);
