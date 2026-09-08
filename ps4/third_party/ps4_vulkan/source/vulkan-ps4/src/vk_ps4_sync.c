/*
 * vk_ps4_sync.c — VkFence / VkSemaphore / VkEvent implementation.
 *
 * Fences and semaphores use GPU-visible label memory (one 4-byte word per
 * sync object, allocated from Garlic direct memory).  When a fence or
 * semaphore is signaled, QueueSubmit appends an EOP event write to the
 * device's epilogue command buffer that writes a monotonically increasing
 * signal_value to the label.  WaitForFences polls the label.
 *
 * VkEvent remains CPU-tracked for now — it is set/reset by the host and
 * inserted into command buffers via CmdSetEvent/CmdResetEvent, which are
 * handled in vk_ps4_command.c.
 */

#include "vk_ps4_internal.h"

#include <string.h>
#ifndef __ORBIS__
#include <time.h>   /* clock_gettime, nanosleep — host only */
#else
#include <orbis/libkernel.h>  /* sceKernelUsleep, sceKernelGetProcessTime */
#endif

/* === Helpers === */

/* Get current time in nanoseconds (monotonic).
 * On host: clock_gettime(CLOCK_MONOTONIC).
 * On PS4: sceKernelGetProcessTime() returns microseconds. */
static uint64_t vk_ps4_now_ns(void) {
#ifndef __ORBIS__
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#else
    return sceKernelGetProcessTime() * 1000ULL;  /* us → ns */
#endif
}

/* Spin hint used before falling back to a scheduler round trip.  PAUSE is not
 * a busy burn: it yields the core's sibling SMT slot and drops power draw. */
static void vk_ps4_sync_pause(void) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    __builtin_ia32_pause();
#endif
}

/* Sleep for approximately `us` microseconds to avoid burning CPU. */
static void vk_ps4_sleep_us(uint32_t us) {
#ifdef __ORBIS__
    sceKernelUsleep(us);
#else
    struct timespec ts = {
        .tv_sec = 0,
        .tv_nsec = (long)us * 1000,
    };
    nanosleep(&ts, NULL);
#endif
}

/* Allocate a 4-byte GPU-visible label for fence/semaphore signaling.
 * Returns VK_SUCCESS and sets up mem->mapped as the label address. */
static VkResult vk_ps4_sync_alloc_label(VkPs4Device *dev, GnmDirectMemory *mem) {
    (void)dev; /* dev reserved for future per-device memory type selection */
    const uint64_t alignment = 64 * 1024; /* 64KB Garlic alignment */
    GnmError err = sceGnmDirectMemoryAllocate(
        mem, 4, alignment,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
    );
    if (err != GNM_ERROR_OK) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return VK_SUCCESS;
}

static void vk_ps4_sync_free_label(GnmDirectMemory *mem) {
    if (mem->allocated) {
        sceGnmDirectMemoryRelease(mem);
    }
    memset(mem, 0, sizeof(*mem));
}

/* === Fence === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
    if (!device || !pCreateInfo || !pFence) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Fence *fence = vk_ps4_alloc_zero(alloc, sizeof(*fence), 16);
    if (!fence) return VK_ERROR_OUT_OF_HOST_MEMORY;
    fence->type = VK_PS4_OBJ_FENCE;
    fence->device = dev;
    fence->signaled = (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0;
    fence->signal_value = fence->signaled ? 1u : 0u;

    /* Allocate GPU label for EOP signaling. */
    VkResult label_result = vk_ps4_sync_alloc_label(dev, &fence->label_mem);
    if (label_result != VK_SUCCESS) {
        vk_ps4_free(alloc, fence);
        return label_result;
    }
    fence->label = (volatile uint32_t *)fence->label_mem.mapped;
    *fence->label = fence->signal_value;

    *pFence = (VkFence)fence;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator) {
    if (!device || !fence) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Fence *f = (VkPs4Fence *)fence;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_sync_free_label(&f->label_mem);
    f->label = NULL;
    vk_ps4_free(alloc, f);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences,
                     VkBool32 waitAll, uint64_t timeout) {
    if (!device || fenceCount == 0) {
        return VK_SUCCESS;
    }

    /* Compute deadline from timeout.  UINT64_MAX = infinite wait. */
    const uint64_t start = vk_ps4_now_ns();
    const uint64_t deadline_ns =
        (timeout == UINT64_MAX) ? UINT64_MAX : start + timeout;

    /* Poll GPU labels.  Each fence is signaled when its label matches its
     * signal_value.  We poll with a yield/sleep to avoid burning CPU.
     * On the host generic build the label is written by the (no-op) submit
     * path, so the CPU-side signaled flag is the source of truth there. */
    uint32_t poll_count = 0;

    for (;;) {
        uint32_t signaled_count = 0;
        for (uint32_t i = 0; i < fenceCount; i++) {
            if (!pFences[i]) continue;
            VkPs4Fence *f = (VkPs4Fence *)pFences[i];
            /* On Orbis, the GPU writes the label via EOP.  On the host
             * generic build, sceGnmSubmitCommandBuffers is a no-op so the
             * GPU never writes the label — the CPU-side signaled flag is
             * the source of truth.  We check signaled first, then the GPU
             * label as the authoritative path on real hardware. */
            /* B39: a fence signalled by a pipelined submit carries a
             * completion ticket rather than an already-written label.
             * Resolving it latches the label once its slot retires, after
             * which the checks below behave exactly as they did in B38. */
            bool done = vk_ps4_sync_resolve_fence(f);
            if (!done && f->label) {
                /* Read the GPU-written label.  volatile read ensures we
                 * re-read on each iteration. */
                uint32_t gpu_val = *f->label;
                done = (gpu_val == f->signal_value);
            }
            if (done) {
                signaled_count++;
                if (!waitAll) {
                    return VK_SUCCESS;
                }
            }
        }
        if (waitAll && signaled_count == fenceCount) {
            return VK_SUCCESS;
        }

        /* Check timeout */
        if (deadline_ns != UINT64_MAX) {
            uint64_t now = vk_ps4_now_ns();
            if (now >= deadline_ns) {
                return VK_TIMEOUT;
            }
        }

        /* Yield to avoid burning CPU.  Use a tiered approach:
         * - First ~2000 polls: PAUSE spin (fast path for quick completion)
         * - Then short sleeps, growing once the wait is clearly not brief.
         *
         * B39: the spin uses PAUSE rather than a bare re-read, and the sleep
         * tier grows.  sceKernelUsleep(1) does not sleep for a microsecond -
         * the kernel rounds up to its scheduling granularity - so a frame
         * fence that was about to signal used to cost a full scheduler round
         * trip, and a genuinely long wait polled far more often than useful. */
        if (poll_count < 2000) {
            vk_ps4_sync_pause();
        } else {
            vk_ps4_sleep_us(poll_count < 8000 ? 20 : 200);
        }
        poll_count++;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences) {
    (void)device;
    for (uint32_t i = 0; i < fenceCount; i++) {
        if (!pFences[i]) continue;
        VkPs4Fence *f = (VkPs4Fence *)pFences[i];
        f->signaled = false;
        /* B39: dropping a pending ticket is correct here.  Vulkan requires a
         * fence passed to vkResetFences to be unsignalled and not in use by
         * a queue submission, so any ticket still attached belongs to a batch
         * the application has already waited for.  Keeping it would make the
         * next resolve latch this fence signalled from the wrong batch. */
        f->pending = (VkPs4CompletionTicket){0, 0};
        /* Don't decrement signal_value — the next signal will increment it
         * to a new value so stale GPU writes don't match. */
        if (f->label) {
            *f->label = 0;
        }
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetFenceStatus(VkDevice device, VkFence fence) {
    (void)device;
    if (!fence) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Fence *f = (VkPs4Fence *)fence;
    /* B39: resolve first so a pipelined batch that has since retired reports
     * VK_SUCCESS.  The async upload path polls this to release staging
     * buffers, so a fence that never resolved would leak them. */
    bool done = vk_ps4_sync_resolve_fence(f);
    if (!done && f->label) {
        done = (*f->label == f->signal_value);
    }
    return done ? VK_SUCCESS : VK_NOT_READY;
}

/* === Semaphore === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore) {
    if (!device || !pCreateInfo || !pSemaphore) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Semaphore *sem = vk_ps4_alloc_zero(alloc, sizeof(*sem), 16);
    if (!sem) return VK_ERROR_OUT_OF_HOST_MEMORY;
    sem->type = VK_PS4_OBJ_SEMAPHORE;
    sem->device = dev;
    sem->signaled = false;
    sem->signal_value = 0u;
    sem->is_timeline = false;
    sem->timeline_value = 0;

    /* Check for VK_KHR_timeline_semaphore via VkSemaphoreTypeCreateInfo in pNext */
    VkBaseInStructure *chain = (VkBaseInStructure *)pCreateInfo->pNext;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR) {
            VkSemaphoreTypeCreateInfoKHR *type_info = (VkSemaphoreTypeCreateInfoKHR *)chain;
            if (type_info->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE_KHR) {
                sem->is_timeline = true;
                sem->timeline_value = type_info->initialValue;
                sem->signaled = (type_info->initialValue > 0);
            }
            break;
        }
        chain = (VkBaseInStructure *)chain->pNext;
    }

    VkResult label_result = vk_ps4_sync_alloc_label(dev, &sem->label_mem);
    if (label_result != VK_SUCCESS) {
        vk_ps4_free(alloc, sem);
        return label_result;
    }
    sem->label = (volatile uint32_t *)sem->label_mem.mapped;
    *sem->label = 0;

    *pSemaphore = (VkSemaphore)sem;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator) {
    if (!device || !semaphore) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Semaphore *sem = (VkPs4Semaphore *)semaphore;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_sync_free_label(&sem->label_mem);
    sem->label = NULL;
    vk_ps4_free(alloc, sem);
}

/* === Event === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkEvent *pEvent) {
    if (!device || !pCreateInfo || !pEvent) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Event *ev = vk_ps4_alloc_zero(alloc, sizeof(*ev), 16);
    if (!ev) return VK_ERROR_OUT_OF_HOST_MEMORY;
    ev->type = VK_PS4_OBJ_EVENT;
    ev->device = dev;
    ev->signaled = false;
    *pEvent = (VkEvent)ev;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *pAllocator) {
    if (!device || !event) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Event *ev = (VkPs4Event *)event;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, ev);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetEventStatus(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Event *ev = (VkPs4Event *)event;
    return ev->signaled ? VK_EVENT_SET : VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_SetEvent(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Event *ev = (VkPs4Event *)event;
    ev->signaled = true;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetEvent(VkDevice device, VkEvent event) {
    (void)device;
    if (!event) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Event *ev = (VkPs4Event *)event;
    ev->signaled = false;
    return VK_SUCCESS;
}

/* === VK_KHR_timeline_semaphore === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetSemaphoreCounterValueKHR(VkDevice device, VkSemaphore semaphore, uint64_t *pValue) {
    (void)device;
    if (!semaphore || !pValue) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Semaphore *sem = (VkPs4Semaphore *)semaphore;
    /* B39: the counter only advances once the batch that signalled it has
     * retired, so resolve before reporting. */
    vk_ps4_sync_resolve_semaphore(sem);
    *pValue = sem->timeline_value;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_SignalSemaphoreKHR(VkDevice device, const VkSemaphoreSignalInfoKHR *pSignalInfo) {
    (void)device;
    if (!pSignalInfo) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Semaphore *sem = (VkPs4Semaphore *)pSignalInfo->semaphore;
    if (!sem || !sem->is_timeline) return VK_ERROR_INITIALIZATION_FAILED;
    /* Timeline semaphore: set the counter to the specified value.
     * The value must be greater than the current value. */
    if (pSignalInfo->value <= sem->timeline_value) {
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    sem->timeline_value = pSignalInfo->value;
    sem->signaled = true;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_WaitSemaphoresKHR(VkDevice device, const VkSemaphoreWaitInfoKHR *pWaitInfo, uint64_t timeout) {
    if (!device || !pWaitInfo) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t deadline = vk_ps4_now_ns() + timeout;
    uint32_t poll_count = 0;
    for (;;) {
        bool all_satisfied = true;
        for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; i++) {
            VkPs4Semaphore *sem = (VkPs4Semaphore *)pWaitInfo->pSemaphores[i];
            if (!sem || !sem->is_timeline) return VK_ERROR_INITIALIZATION_FAILED;
            /* B39: pick up a counter advance published by a batch that has
             * retired since the last poll. */
            vk_ps4_sync_resolve_semaphore(sem);
            if (sem->timeline_value < pWaitInfo->pValues[i]) {
                all_satisfied = false;
                break;
            }
        }
        if (all_satisfied) return VK_SUCCESS;
        if (timeout != UINT64_MAX && vk_ps4_now_ns() >= deadline)
            return VK_TIMEOUT;
        if (poll_count < 2000) {
            vk_ps4_sync_pause();
        } else {
            vk_ps4_sleep_us(poll_count < 8000 ? 20 : 200);
        }
        poll_count++;
    }
}
