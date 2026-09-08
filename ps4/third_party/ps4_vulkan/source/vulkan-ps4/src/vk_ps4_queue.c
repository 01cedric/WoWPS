/*
 * vk_ps4_queue.c - conservative PS4 queue submission.
 *
 * The original implementation reused one small epilogue DCB for a wait
 * packet, every signal semaphore, the fence, and the next frame without ever
 * proving that the GPU had stopped fetching it. On real hardware the CPU can
 * therefore overwrite queued PM4. It also omitted sceGnmSubmitDone(), so FW
 * 5.05 could leave the batch uncommitted. Both failures can present zero-filled
 * buffers and eventually fault GNM.
 *
 * B38 and earlier answered that by serializing: user DCBs are submitted, one
 * completion EOP is emitted, sceGnmSubmitDone closes the batch, and the CPU
 * waits for that EOP before returning. Vulkan semaphores/fences were
 * published only after completion. That is safe, and it makes epilogue reuse
 * impossible while a batch is in flight - but it costs all CPU/GPU overlap,
 * so a frame costs CPU + GPU end to end and every streaming upload batch is
 * a full pipeline flush.
 *
 * B39 keeps the same completion proof and removes the serialization. The
 * device owns a small ring of completion slots (VK_PS4_MAX_SUBMIT_SLOTS),
 * each with its own epilogue DCB and its own GPU-written label, so the "never
 * overwrite PM4 the CP may still fetch" invariant now holds by construction
 * rather than by waiting. A submit emits its EOP into a free slot and returns
 * a ticket; the fence and semaphores it signals carry that ticket and resolve
 * it when queried. A slot is only reused once its label proves the GPU passed
 * it, and QueueSubmit still waits when the ring is full, so the number of
 * outstanding batches is bounded.
 *
 * WOWEE_VK_SUBMIT_MODE=serial restores the B38 path byte for byte.
 */

#include "vk_ps4_internal.h"

#include <stdlib.h>
#include <string.h>

#if defined(__ORBIS__)
/* Keep this translation unit independent of the legacy libkernel header's
 * incomplete sched_param declaration (which breaks strict -Werror builds). */
extern uint64_t sceKernelGetProcessTime(void);
extern int32_t sceKernelUsleep(uint32_t microseconds);
#endif

/* wow_ps B4: 20 s. The first world frame after a load uploads a great deal of
 * texture and geometry in one batch; a 5 s verdict of "device lost" on a slow
 * but healthy batch was permanent for the session. */
#define VK_PS4_QUEUE_COMPLETION_TIMEOUT_US 20000000ULL

typedef struct VkPs4QueueWaitSample {
    uint64_t native_us;
    uint64_t completion_wait_us;
    uint64_t polls;
    uint64_t max_poll_sleep_us;
} VkPs4QueueWaitSample;

static uint64_t vk_ps4_queue_clock_us(void) {
#if defined(__ORBIS__)
    return sceKernelGetProcessTime();
#else
    return 0;
#endif
}

/* B21: distinguish native submission cost, completion waiting, and coarse
 * kernel wakeups. Completion waiting includes GPU work and any GPU-side
 * scanout wait; it is deliberately NOT labelled a measured GPU draw time.
 * Only batches >= 64 KiB are included, so thousands of tiny upload/idle
 * submissions do not conceal a slow world frame. The statistic owns no
 * allocations and changes neither the polling interval nor completion proof. */
static void vk_ps4_queue_record_performance(
    VkPs4Queue *q, uint64_t bytes, uint32_t dcbs,
    const VkPs4QueueWaitSample *sample
) {
    if (bytes < 64u * 1024u) return;
    const uint64_t now = vk_ps4_queue_clock_us();
    VkPs4QueuePerformance *p = &q->performance;
    if (!p->samples) p->window_start_us = now;
    ++p->samples;
    p->user_bytes += bytes;
    p->user_dcbs += dcbs;
    p->native_us += sample->native_us;
    p->completion_wait_us += sample->completion_wait_us;
    if (sample->completion_wait_us > p->max_completion_wait_us)
        p->max_completion_wait_us = sample->completion_wait_us;
    p->polls += sample->polls;
    if (sample->max_poll_sleep_us > p->max_poll_sleep_us)
        p->max_poll_sleep_us = sample->max_poll_sleep_us;
    if (now - p->window_start_us < 5000000u) return;
    vk_ps4_log("B21 queue performance: samples=%llu minBatchKiB=64 "
               "meanKiB=%llu userDCBs=%llu nativeMeanUs=%llu "
               "completionWaitMeanUs=%llu completionWaitMaxUs=%llu "
               "pollsMean=%llu pollSleepMaxUs=%llu",
               (unsigned long long)p->samples,
               (unsigned long long)(p->user_bytes / p->samples / 1024u),
               (unsigned long long)p->user_dcbs,
               (unsigned long long)(p->native_us / p->samples),
               (unsigned long long)(p->completion_wait_us / p->samples),
               (unsigned long long)p->max_completion_wait_us,
               (unsigned long long)(p->polls / p->samples),
               (unsigned long long)p->max_poll_sleep_us);
    *p = (VkPs4QueuePerformance){0};
}

/* Keep enough early breadcrumbs to diagnose the complete M6.0.1 120-frame
 * probe, then avoid a synchronous file flush on every frame during M6.2
 * world rendering.  Error paths remain unconditionally logged. */
static bool vk_ps4_queue_trace_success(uint64_t serial) {
    return serial <= 128u || (serial % 120u) == 0u;
}

static void vk_ps4_queue_sfence(void) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    __asm__ volatile("sfence" ::: "memory");
#endif
}

/* B39: one poll step of a GPU-label wait.
 *
 * The B38 loops called sceKernelUsleep(50) on the first miss. The PS4 kernel
 * rounds a sleep up to its scheduling granularity, so a "50 us" sleep is
 * routinely one to two orders of magnitude longer - the B21 statistic that
 * records max_poll_sleep_us exists because of exactly that. Every completion
 * and flip wait therefore paid a scheduler round trip even when the label was
 * about to land microseconds later.
 *
 * Spinning first with PAUSE covers the short waits at no syscall cost (PAUSE
 * is a hint, not a busy burn: it yields the core's other SMT slot and lowers
 * power on the Jaguar). Only a wait that outlives the spin budget hands the
 * core back to the scheduler, and it then sleeps in growing steps so a long
 * wait does not poll thousands of times either. */
#define VK_PS4_QUEUE_SPIN_POLLS 2048u

static void vk_ps4_queue_poll_pause(void) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    __builtin_ia32_pause();
#endif
}

/* Returns true when this step handed the core back to the scheduler, so the
 * B21 statistic keeps counting coarse kernel wakeups rather than spins. */
static bool vk_ps4_queue_poll_step(uint32_t poll_index) {
    if (poll_index < VK_PS4_QUEUE_SPIN_POLLS) {
        vk_ps4_queue_poll_pause();
        return false;
    }
#if defined(__ORBIS__)
    /* 20 us for the first stretch past the spin budget, then 200 us. Both are
     * below the kernel's granularity, so they act as "yield now" requests of
     * increasing patience rather than precise delays. */
    sceKernelUsleep(poll_index < VK_PS4_QUEUE_SPIN_POLLS + 4096u ? 20u : 200u);
    return true;
#else
    (void)poll_index;
    return false;
#endif
}

/* === B39 submit serialization ===
 *
 * vkQueueSubmit is reachable from more than one thread in this client: the
 * frame submit runs on the main thread while texture and buffer uploads are
 * submitted from loader threads. B38 left that unguarded - VkPs4Queue even
 * carries an unused submit_mutex field - and got away with it mostly because
 * every submit drained the GPU, which made overlapping calls rare.
 *
 * Pipelining removes that accidental serialization, so the shared state
 * (gnm_completion_value, the slot ring cursor, and GNM's own submission
 * sequence) needs a real one. This is a plain test-and-set lock rather than a
 * pthread mutex so the ICD keeps its current zero-dependency build; the
 * waiting side reuses the same spin-then-sleep backoff as the label waits, so
 * a thread blocked behind a full ring hands its core back rather than
 * burning it. */
static volatile int g_vk_ps4_submit_lock;

static void vk_ps4_queue_lock(void) {
    uint32_t polls = 0;
    while (__atomic_test_and_set(&g_vk_ps4_submit_lock, __ATOMIC_ACQUIRE)) {
        vk_ps4_queue_poll_step(polls++);
    }
}

static void vk_ps4_queue_unlock(void) {
    __atomic_clear(&g_vk_ps4_submit_lock, __ATOMIC_RELEASE);
}

/* === B39 completion slot ring === */

/* Each slot needs room for one EOP packet plus a trailing label word. The
 * legacy single epilogue buffer used 64 dwords; keep that per slot so a slot
 * DCB is byte-for-byte the shape the serial path already proved on hardware. */
#define VK_PS4_QUEUE_SLOT_DWORDS 64u

VkPs4SubmitMode vk_ps4_queue_submit_mode(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("WOWEE_VK_SUBMIT_MODE");
        cached = (v && (strcmp(v, "serial") == 0 || strcmp(v, "0") == 0))
            ? (int)VK_PS4_SUBMIT_MODE_SERIAL
            : (int)VK_PS4_SUBMIT_MODE_PIPELINED;
    }
    return (VkPs4SubmitMode)cached;
}

VkResult vk_ps4_queue_init_completion_ring(VkPs4Device *dev) {
    if (!dev) return VK_ERROR_INITIALIZATION_FAILED;
    dev->gnm_submit_mode = vk_ps4_queue_submit_mode();
    dev->gnm_submit_slot_count = 0;
    dev->gnm_submit_next_slot = 0;
    dev->gnm_submit_newest = (VkPs4CompletionTicket){0, 0};
    memset(dev->gnm_submit_slots, 0, sizeof(dev->gnm_submit_slots));
    if (dev->gnm_submit_mode == VK_PS4_SUBMIT_MODE_SERIAL) {
        vk_ps4_log_raw("B39 submit ring: disabled (WOWEE_VK_SUBMIT_MODE=serial)");
        return VK_SUCCESS;
    }

    const uint64_t bytes =
        (uint64_t)VK_PS4_MAX_SUBMIT_SLOTS * VK_PS4_QUEUE_SLOT_DWORDS *
        sizeof(uint32_t);
    const uint64_t alignment = 64u * 1024u;
    const GnmError err = sceGnmDirectMemoryAllocate(
        &dev->gnm_submit_ring_mem, bytes, alignment,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
    );
    if (err != GNM_ERROR_OK || !dev->gnm_submit_ring_mem.mapped) {
        /* Not fatal. The legacy single-epilogue serial path stays available
         * and is what the device will use for the rest of the session. */
        vk_ps4_log("B39 submit ring: allocation FAILED rc=%d; using serial submit",
                   (int)err);
        dev->gnm_submit_mode = VK_PS4_SUBMIT_MODE_SERIAL;
        return VK_SUCCESS;
    }

    uint32_t *base = (uint32_t *)dev->gnm_submit_ring_mem.mapped;
    memset(base, 0, (size_t)bytes);
    for (uint32_t i = 0; i < VK_PS4_MAX_SUBMIT_SLOTS; ++i) {
        VkPs4CompletionSlot *slot = &dev->gnm_submit_slots[i];
        slot->dcb = base + (size_t)i * VK_PS4_QUEUE_SLOT_DWORDS;
        slot->dcb_dwords = VK_PS4_QUEUE_SLOT_DWORDS;
        /* Same layout as the legacy buffer: the last dword is the label. */
        slot->label = (volatile uint32_t *)(slot->dcb +
            VK_PS4_QUEUE_SLOT_DWORDS - 1u);
        slot->value = 0;
        slot->issued = false;
    }
    dev->gnm_submit_slot_count = VK_PS4_MAX_SUBMIT_SLOTS;
    vk_ps4_log("B39 submit ring: %u slots x %u dwords, pipelined submit enabled",
               VK_PS4_MAX_SUBMIT_SLOTS, VK_PS4_QUEUE_SLOT_DWORDS);
    return VK_SUCCESS;
}

void vk_ps4_queue_release_completion_ring(VkPs4Device *dev) {
    if (!dev) return;
    if (dev->gnm_submit_ring_mem.allocated) {
        sceGnmDirectMemoryRelease(&dev->gnm_submit_ring_mem);
    }
    memset(&dev->gnm_submit_ring_mem, 0, sizeof(dev->gnm_submit_ring_mem));
    memset(dev->gnm_submit_slots, 0, sizeof(dev->gnm_submit_slots));
    dev->gnm_submit_slot_count = 0;
    dev->gnm_submit_next_slot = 0;
    dev->gnm_submit_newest = (VkPs4CompletionTicket){0, 0};
}

static bool vk_ps4_queue_slot_reached(const VkPs4CompletionSlot *slot) {
    if (!slot || !slot->label) return true;
    if (!slot->issued) return true;
#if defined(__ORBIS__)
    return *slot->label == slot->value;
#else
    /* Generic GNM submits are no-ops, so nothing ever writes the label. */
    return true;
#endif
}

bool vk_ps4_queue_ticket_complete(VkPs4Device *dev,
                                  const VkPs4CompletionTicket *ticket) {
    if (!dev || !ticket || ticket->slot_index == 0) return true;
    const uint32_t index = ticket->slot_index - 1u;
    if (index >= dev->gnm_submit_slot_count) return true;
    const VkPs4CompletionSlot *slot = &dev->gnm_submit_slots[index];
    if (!slot->label) return true;
    /* The slot may already have been recycled for a newer batch. Recycling
     * only happens after its previous value was observed, so a mismatched
     * slot value means this ticket completed some time ago. */
    if (slot->value != ticket->value) return true;
#if defined(__ORBIS__)
    return *slot->label == ticket->value;
#else
    return true;
#endif
}

bool vk_ps4_queue_ticket_wait(VkPs4Device *dev,
                              const VkPs4CompletionTicket *ticket) {
    if (!dev || !ticket || ticket->slot_index == 0) return true;
    if (vk_ps4_queue_ticket_complete(dev, ticket)) return true;
#if defined(__ORBIS__)
    const uint64_t start = sceKernelGetProcessTime();
    uint32_t polls = 0;
    while (!vk_ps4_queue_ticket_complete(dev, ticket)) {
        if (sceKernelGetProcessTime() - start >=
            VK_PS4_QUEUE_COMPLETION_TIMEOUT_US) {
            vk_ps4_log("B39 ticket wait TIMEOUT slot=%u value=%u",
                       ticket->slot_index - 1u, ticket->value);
            dev->gnm_device_lost = true;
            return false;
        }
        vk_ps4_queue_poll_step(polls++);
    }
#endif
    return true;
}

/* Pick a slot whose previous batch is known finished. Slots are issued in
 * round-robin order and a single graphics ring retires them in that same
 * order, so the round-robin cursor is also the oldest outstanding slot - if
 * it has not retired, no later slot has either, and waiting here is what
 * bounds the number of batches in flight. */
static VkPs4CompletionSlot *vk_ps4_queue_acquire_slot(
    VkPs4Device *dev, uint32_t *out_index
) {
    if (!dev || dev->gnm_submit_slot_count == 0) return NULL;
    const uint32_t index = dev->gnm_submit_next_slot;
    VkPs4CompletionSlot *slot = &dev->gnm_submit_slots[index];
    if (slot->issued && !vk_ps4_queue_slot_reached(slot)) {
        const VkPs4CompletionTicket pending = {index + 1u, slot->value};
        if (!vk_ps4_queue_ticket_wait(dev, &pending)) return NULL;
    }
    slot->issued = false;
    dev->gnm_submit_next_slot =
        (index + 1u) % dev->gnm_submit_slot_count;
    if (out_index) *out_index = index;
    return slot;
}

/* Build one EOP packet at the start of an epilogue DCB. */
static uint32_t vk_ps4_queue_emit_completion_into(
    uint32_t *dcb, uint32_t dcb_dwords, uint64_t gpuaddr, uint32_t value
) {
    if (!dcb || dcb_dwords < 8) return 0;
    GnmCommandBuffer cmd = sceGnmCmdInit(
        dcb, dcb_dwords * sizeof(uint32_t), NULL, NULL
    );
    sceGnmDrawCmdEventWriteEop(
        &cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT, gpuaddr,
        GNM_DATA_SEL_SEND_DATA32, (uint64_t)value
    );
    uint32_t used = (uint32_t)(cmd.cmdptr - cmd.beginptr);
    /* The last dword is the completion label, not command storage. */
    if (used == 0 || used >= dcb_dwords - 1) return 0;
    return used * sizeof(uint32_t);
}

/* Build one EOP packet at the start of the device epilogue DCB. */
static uint32_t vk_ps4_queue_emit_completion(
    VkPs4Device *dev, uint64_t gpuaddr, uint32_t value
) {
    if (!dev || !dev->gnm_epilogue_cmd || dev->gnm_epilogue_cmd_dwords < 8) {
        return 0;
    }
    GnmCommandBuffer cmd = sceGnmCmdInit(
        dev->gnm_epilogue_cmd,
        dev->gnm_epilogue_cmd_dwords * sizeof(uint32_t), NULL, NULL
    );
    sceGnmDrawCmdEventWriteEop(
        &cmd, GNM_CACHE_FLUSH_AND_INV_TS_EVENT, gpuaddr,
        GNM_DATA_SEL_SEND_DATA32, (uint64_t)value
    );
    uint32_t used = (uint32_t)(cmd.cmdptr - cmd.beginptr);
    /* The last dword is the completion label, not command storage. */
    if (used == 0 || used >= dev->gnm_epilogue_cmd_dwords - 1) return 0;
    return used * sizeof(uint32_t);
}

static bool vk_ps4_queue_wait_label(
    volatile uint32_t *label, uint32_t expected,
    VkPs4QueueWaitSample *sample
) {
    if (!label) return false;
#if defined(__ORBIS__)
    const uint64_t start = sceKernelGetProcessTime();
    uint32_t polls = 0;
    while (*label != expected) {
        const uint64_t now = sceKernelGetProcessTime();
        if (now - start >= VK_PS4_QUEUE_COMPLETION_TIMEOUT_US) {
            if (sample) sample->completion_wait_us = now - start;
            return false;
        }
        const bool slept = vk_ps4_queue_poll_step(polls++);
        if (sample && slept) {
            /* Only scheduler round trips are recorded: the spin tier before
             * them makes no syscall, so counting it would drown the statistic
             * this field exists for. */
            const uint64_t sleep_us = sceKernelGetProcessTime() - now;
            ++sample->polls;
            if (sleep_us > sample->max_poll_sleep_us)
                sample->max_poll_sleep_us = sleep_us;
        }
    }
    if (sample) sample->completion_wait_us = sceKernelGetProcessTime() - start;
#else
    /* Generic GNM submits are no-ops. */
    *label = expected;
#endif
    return true;
}

/* Close the current GNM batch and wait for all preceding DCBs. */
static VkResult vk_ps4_queue_finish_batch(
    VkPs4Device *dev, bool trace, VkPs4QueueWaitSample *sample
) {
    if (!dev || !dev->gnm_epilogue_cmd ||
        dev->gnm_epilogue_cmd_dwords < 8 || dev->gnm_device_lost) {
        return VK_ERROR_DEVICE_LOST;
    }

    volatile uint32_t *label =
        (volatile uint32_t *)(dev->gnm_epilogue_cmd +
            dev->gnm_epilogue_cmd_dwords - 1);
    uint32_t value = ++dev->gnm_completion_value;
    if (value == 0) value = ++dev->gnm_completion_value;
    *label = value ^ 0xffffffffu;

    uint32_t epilogue_bytes = vk_ps4_queue_emit_completion(
        dev, (uint64_t)(uintptr_t)label, value
    );
    if (epilogue_bytes == 0) {
        vk_ps4_log_raw("QueueSubmit: completion EOP overflow/invalid");
        dev->gnm_device_lost = true;
        return VK_ERROR_DEVICE_LOST;
    }

    vk_ps4_queue_sfence();
    void *epilogue_addr = dev->gnm_epilogue_cmd;
    if (trace) vk_ps4_log("B18 submit: completion DCB begin value=%u", value);
    const uint64_t epilogue_start = sample ? vk_ps4_queue_clock_us() : 0;
    int32_t result = sceGnmSubmitCommandBuffers(
        1, &epilogue_addr, &epilogue_bytes, NULL, NULL
    );
    if (sample) sample->native_us += vk_ps4_queue_clock_us() - epilogue_start;
    if (result != 0) {
        vk_ps4_log("QueueSubmit: completion DCB submit FAILED rc=%d", result);
        dev->gnm_device_lost = true;
        return VK_ERROR_DEVICE_LOST;
    }

    if (trace) vk_ps4_log("B18 submit: completion DCB accepted; SubmitDone begin value=%u", value);
    const uint64_t done_start = sample ? vk_ps4_queue_clock_us() : 0;
    result = sceGnmSubmitDone();
    if (sample) sample->native_us += vk_ps4_queue_clock_us() - done_start;
    if (result != 0) {
        vk_ps4_log("QueueSubmit: sceGnmSubmitDone FAILED rc=%d", result);
        dev->gnm_device_lost = true;
        return VK_ERROR_DEVICE_LOST;
    }

    if (trace) vk_ps4_log("B18 submit: SubmitDone returned; GPU wait begin value=%u", value);
    if (!vk_ps4_queue_wait_label(label, value, sample)) {
        vk_ps4_log("QueueSubmit: GPU completion TIMEOUT value=%u observed=%u",
                   value, (unsigned)*label);
        dev->gnm_device_lost = true;
        return VK_ERROR_DEVICE_LOST;
    }
    if (trace || vk_ps4_queue_trace_success(value))
        vk_ps4_log("QueueSubmit: batch complete value=%u", value);
    return VK_SUCCESS;
}

/* B39: close the current GNM batch into a completion slot and return its
 * ticket without waiting. Everything up to sceGnmSubmitDone is identical to
 * the serial path; only the final wait_label is replaced by handing the
 * caller a ticket that fences and semaphores resolve later. */
static VkResult vk_ps4_queue_finish_batch_pipelined(
    VkPs4Device *dev, bool trace, VkPs4QueueWaitSample *sample,
    VkPs4CompletionTicket *out_ticket
) {
    if (!dev || dev->gnm_device_lost) return VK_ERROR_DEVICE_LOST;
    if (dev->gnm_submit_slot_count == 0) {
        return vk_ps4_queue_finish_batch(dev, trace, sample);
    }

    uint32_t slot_index = 0;
    VkPs4CompletionSlot *slot = vk_ps4_queue_acquire_slot(dev, &slot_index);
    if (!slot || !slot->dcb || !slot->label) {
        /* The ring could not produce a retired slot (device already lost, or
         * the wait above timed out). Never fall back to reusing a slot the
         * GPU may still be fetching. */
        return VK_ERROR_DEVICE_LOST;
    }

    uint32_t value = ++dev->gnm_completion_value;
    if (value == 0) value = ++dev->gnm_completion_value;
    /* Seed with the complement so a stale identical value cannot pass. */
    *slot->label = value ^ 0xffffffffu;

    uint32_t epilogue_bytes = vk_ps4_queue_emit_completion_into(
        slot->dcb, slot->dcb_dwords, (uint64_t)(uintptr_t)slot->label, value
    );
    if (epilogue_bytes == 0) {
        vk_ps4_log_raw("B39 submit: completion EOP overflow/invalid");
        dev->gnm_device_lost = true;
        return VK_ERROR_DEVICE_LOST;
    }

    vk_ps4_queue_sfence();
    void *epilogue_addr = slot->dcb;
    const uint64_t epilogue_start = sample ? vk_ps4_queue_clock_us() : 0;
    int32_t result = sceGnmSubmitCommandBuffers(
        1, &epilogue_addr, &epilogue_bytes, NULL, NULL
    );
    if (sample) sample->native_us += vk_ps4_queue_clock_us() - epilogue_start;
    if (result != 0) {
        vk_ps4_log("B39 submit: completion DCB submit FAILED rc=%d", result);
        dev->gnm_device_lost = true;
        return VK_ERROR_DEVICE_LOST;
    }

    const uint64_t done_start = sample ? vk_ps4_queue_clock_us() : 0;
    result = sceGnmSubmitDone();
    if (sample) sample->native_us += vk_ps4_queue_clock_us() - done_start;
    if (result != 0) {
        vk_ps4_log("B39 submit: sceGnmSubmitDone FAILED rc=%d", result);
        dev->gnm_device_lost = true;
        return VK_ERROR_DEVICE_LOST;
    }

    slot->value = value;
    slot->issued = true;
    const VkPs4CompletionTicket ticket = {slot_index + 1u, value};
    dev->gnm_submit_newest = ticket;
    if (out_ticket) *out_ticket = ticket;
    if (trace)
        vk_ps4_log("B39 submit: batch issued slot=%u value=%u (no CPU wait)",
                   slot_index, value);
    return VK_SUCCESS;
}

/* Wait for every batch this device has issued. Used by Queue/DeviceWaitIdle
 * and by teardown paths that must prove the GPU released their memory. */
static VkResult vk_ps4_queue_drain(VkPs4Device *dev) {
    if (!dev) return VK_ERROR_INITIALIZATION_FAILED;
    if (dev->gnm_device_lost) return VK_ERROR_DEVICE_LOST;
    if (dev->gnm_submit_slot_count == 0) return VK_SUCCESS;
    for (uint32_t i = 0; i < dev->gnm_submit_slot_count; ++i) {
        VkPs4CompletionSlot *slot = &dev->gnm_submit_slots[i];
        if (!slot->issued) continue;
        const VkPs4CompletionTicket ticket = {i + 1u, slot->value};
        if (!vk_ps4_queue_ticket_wait(dev, &ticket)) return VK_ERROR_DEVICE_LOST;
        slot->issued = false;
    }
    return VK_SUCCESS;
}

bool vk_ps4_sync_resolve_fence(VkPs4Fence *fence) {
    if (!fence) return false;
    if (fence->pending.slot_index == 0) return fence->signaled;
    if (!vk_ps4_queue_ticket_complete(fence->device, &fence->pending)) {
        return false;
    }
    /* Latch: publishing the label makes GetFenceStatus and WaitForFences see
     * a signalled fence through their existing label comparison too. */
    if (fence->label) *fence->label = fence->signal_value;
    fence->signaled = true;
    fence->pending = (VkPs4CompletionTicket){0, 0};
    return true;
}

bool vk_ps4_sync_resolve_semaphore(VkPs4Semaphore *sem) {
    if (!sem) return false;
    if (sem->pending.slot_index == 0) return sem->signaled;
    if (!vk_ps4_queue_ticket_complete(sem->device, &sem->pending)) return false;
    if (sem->label) *sem->label = sem->signal_value;
    if (sem->is_timeline && sem->pending_timeline_value > sem->timeline_value) {
        sem->timeline_value = sem->pending_timeline_value;
    }
    sem->signaled = true;
    sem->pending = (VkPs4CompletionTicket){0, 0};
    return true;
}

static bool vk_ps4_queue_wait_semaphore(VkPs4Semaphore *sem) {
    if (!sem) return true;
    /* B39: a semaphore signalled by an outstanding pipelined batch is not
     * ready until that batch retires. Resolving first keeps the existing
     * checks below correct rather than duplicating them. */
    if (sem->pending.slot_index != 0 && sem->device) {
        if (!vk_ps4_queue_ticket_wait(sem->device, &sem->pending)) return false;
        vk_ps4_sync_resolve_semaphore(sem);
    }
    if (sem->signaled) return true;
    if (sem->label && sem->signal_value != 0 &&
        *sem->label == sem->signal_value) {
        return true;
    }
#if defined(__ORBIS__)
    const uint64_t start = sceKernelGetProcessTime();
    uint32_t polls = 0;
    for (;;) {
        if (sem->signaled ||
            (sem->label && sem->signal_value != 0 &&
             *sem->label == sem->signal_value)) {
            return true;
        }
        if (sceKernelGetProcessTime() - start >=
            VK_PS4_QUEUE_COMPLETION_TIMEOUT_US) {
            return false;
        }
        vk_ps4_queue_poll_step(polls++);
    }
#else
    /* Preserve the generic serialized test backend's permissive behavior. */
    return true;
#endif
}

static VkResult vk_ps4_queue_submit_locked(
    VkQueue queue, uint32_t submitCount,
    const VkSubmitInfo *pSubmits, VkFence fence
) {
    VkPs4Queue *q = (VkPs4Queue *)queue;
    VkPs4Device *dev = q->device;
    if (!dev || dev->gnm_device_lost) return VK_ERROR_DEVICE_LOST;

    /* Uploads push the first world frame past the startup/every-120 filter.
     * Always trace the first eight submissions containing compute, including
     * native-call return boundaries and the actual GPU completion wait. */
    bool compute_submit = false;
    for (uint32_t i = 0; i < submitCount; ++i) {
        for (uint32_t c = 0; c < pSubmits[i].commandBufferCount; ++c) {
            const VkPs4CommandBuffer *cmd =
                (const VkPs4CommandBuffer *)pSubmits[i].pCommandBuffers[c];
            if (cmd && cmd->compute_dispatch_count) compute_submit = true;
        }
    }
    const bool trace_compute = compute_submit && dev->gnm_compute_trace_count < 8u;
    if (trace_compute) ++dev->gnm_compute_trace_count;
    const bool trace_success = trace_compute || vk_ps4_queue_trace_success(
        (uint64_t)dev->gnm_completion_value + 1u);
    if (trace_success) {
        vk_ps4_log("QueueSubmit: count=%u family=%u SERIAL_SAFE",
                   submitCount, q->family_index);
    }
    bool submitted_any = false;
    uint64_t user_bytes = 0;
    uint32_t user_dcbs = 0;
    VkPs4QueueWaitSample sample = {0};

    for (uint32_t i = 0; i < submitCount; i++) {
        const VkSubmitInfo *submit = &pSubmits[i];

        /* Queue submissions are synchronous, so a CPU wait is sufficient and
         * avoids a prefix WaitMem DCB needing separate lifetime tracking.
         * AcquireNextImageKHR signals its semaphore on the CPU. */
        for (uint32_t w = 0; w < submit->waitSemaphoreCount; w++) {
            VkPs4Semaphore *sem =
                (VkPs4Semaphore *)submit->pWaitSemaphores[w];
            if (!vk_ps4_queue_wait_semaphore(sem)) {
                vk_ps4_log("QueueSubmit: wait semaphore TIMEOUT index=%u", w);
                dev->gnm_device_lost = true;
                return VK_ERROR_DEVICE_LOST;
            }
            if (sem) sem->signaled = false;
        }

        for (uint32_t c = 0; c < submit->commandBufferCount; c++) {
            VkPs4CommandBuffer *cmd =
                (VkPs4CommandBuffer *)submit->pCommandBuffers[c];
            if (cmd && cmd->recording_error != VK_SUCCESS) {
                vk_ps4_log("QueueSubmit: rejected failed command recording result=%d", (int)cmd->recording_error);
                return cmd->recording_error;
            }
            if (!cmd || !cmd->pm4_buffer || cmd->pm4_used == 0) continue;
            const uint32_t count = cmd->pm4_segment_count;
            if (!count || count > VK_PS4_MAX_PM4_SEGMENTS) return VK_ERROR_INITIALIZATION_FAILED;
            void *addresses[VK_PS4_MAX_PM4_SEGMENTS];
            uint32_t sizes[VK_PS4_MAX_PM4_SEGMENTS];
            uint64_t total_bytes = 0;
            for (uint32_t segment = 0; segment < count; ++segment) {
                const GnmDirectMemory *mem = segment ? &cmd->pm4_extra[segment - 1u] : &cmd->pm4_mem;
                const uint32_t used = cmd->pm4_segment_used[segment];
                if (!used || used > cmd->pm4_buffer_size || !mem->allocated || !mem->mapped) {
                    vk_ps4_log("QueueSubmit: invalid mapped PM4 segment=%u used=%u", segment, used);
                    dev->gnm_device_lost = true;
                    return VK_ERROR_DEVICE_LOST;
                }
                addresses[segment] = mem->mapped;
                sizes[segment] = used * sizeof(uint32_t);
                total_bytes += sizes[segment];
            }
            if (trace_success)
                vk_ps4_log("QueueSubmit: %u immutable mapped DCB segments bytes=%llu", count,
                           (unsigned long long)total_bytes);
            vk_ps4_queue_sfence();
            const uint64_t native_start = vk_ps4_queue_clock_us();
            int32_t result = sceGnmSubmitCommandBuffers(count, addresses, sizes, NULL, NULL);
            sample.native_us += vk_ps4_queue_clock_us() - native_start;
            if (result != 0) {
                vk_ps4_log("QueueSubmit: segmented DCB FAILED rc=%d segments=%u", result, count);
                dev->gnm_device_lost = true;
                return VK_ERROR_DEVICE_LOST;
            }
            submitted_any = true;
            user_bytes += total_bytes;
            user_dcbs += count;
        }
    }

    /* B39: a submission with no user DCB (an empty command buffer, or one
     * whose recording produced nothing) signals immediately as before - there
     * is no GPU work to wait for. Only a real batch produces a ticket. */
    VkPs4CompletionTicket ticket = {0, 0};
    const bool pipelined =
        dev->gnm_submit_mode == VK_PS4_SUBMIT_MODE_PIPELINED &&
        dev->gnm_submit_slot_count > 0;

    if (submitted_any) {
        VkPs4QueueWaitSample *const sample_ptr =
            user_bytes >= 64u * 1024u ? &sample : NULL;
        const VkResult finish = pipelined
            ? vk_ps4_queue_finish_batch_pipelined(
                  dev, trace_success, sample_ptr, &ticket)
            : vk_ps4_queue_finish_batch(dev, trace_success, sample_ptr);
        if (finish != VK_SUCCESS) return finish;
        vk_ps4_queue_record_performance(q, user_bytes, user_dcbs, &sample);
    }

    /* Publish semaphore and fence state.
     *
     * Serial mode has already proved GPU completion, so the label is written
     * straight away exactly as in B38. Pipelined mode records the batch's
     * ticket instead: signal_value is reserved now (so a later Reset/Wait
     * compares against the right number) but the label is only written once
     * vk_ps4_sync_resolve_* observes the slot retire. That is what keeps
     * WaitForFences, GetFenceStatus, a timeline wait and QueuePresent from
     * observing a frame the GPU has not finished. */
    for (uint32_t i = 0; i < submitCount; i++) {
        const VkSubmitInfo *submit = &pSubmits[i];
        const VkTimelineSemaphoreSubmitInfo *timeline = NULL;
        for (const VkBaseInStructure *chain =
                 (const VkBaseInStructure *)submit->pNext;
             chain; chain = chain->pNext) {
            if (chain->sType ==
                VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
                timeline = (const VkTimelineSemaphoreSubmitInfo *)chain;
                break;
            }
        }
        for (uint32_t s = 0; s < submit->signalSemaphoreCount; s++) {
            VkPs4Semaphore *sem =
                (VkPs4Semaphore *)submit->pSignalSemaphores[s];
            if (!sem) continue;
            if (++sem->signal_value == 0) ++sem->signal_value;
            /* A timeline signal carries the value the counter takes when the
             * batch retires. B38 dropped pSignalSemaphoreValues entirely,
             * which left every timeline counter at its initial value. */
            uint64_t timeline_target = 0;
            if (sem->is_timeline && timeline &&
                s < timeline->signalSemaphoreValueCount &&
                timeline->pSignalSemaphoreValues) {
                timeline_target = timeline->pSignalSemaphoreValues[s];
            }
            if (ticket.slot_index != 0) {
                sem->pending = ticket;
                sem->pending_timeline_value = timeline_target;
                sem->signaled = false;
            } else {
                if (sem->label) *sem->label = sem->signal_value;
                if (timeline_target > sem->timeline_value)
                    sem->timeline_value = timeline_target;
                sem->signaled = true;
                sem->pending = (VkPs4CompletionTicket){0, 0};
            }
        }
    }
    if (fence) {
        VkPs4Fence *f = (VkPs4Fence *)fence;
        if (++f->signal_value == 0) ++f->signal_value;
        if (ticket.slot_index != 0) {
            f->pending = ticket;
            f->signaled = false;
        } else {
            if (f->label) *f->label = f->signal_value;
            f->signaled = true;
            f->pending = (VkPs4CompletionTicket){0, 0};
        }
    }
    vk_ps4_queue_sfence();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_QueueSubmit(VkQueue queue, uint32_t submitCount,
                   const VkSubmitInfo *pSubmits, VkFence fence) {
    if (!queue || (submitCount > 0 && !pSubmits)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    /* One submission at a time: the GNM submission sequence, the completion
     * counter and the slot ring are all device-wide state. */
    vk_ps4_queue_lock();
    const VkResult result =
        vk_ps4_queue_submit_locked(queue, submitCount, pSubmits, fence);
    vk_ps4_queue_unlock();
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_QueueWaitIdle(VkQueue queue) {
    if (!queue) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Queue *q = (VkPs4Queue *)queue;
    if (!q->device || q->device->gnm_device_lost) {
        return VK_ERROR_DEVICE_LOST;
    }
    /* An explicit idle checkpoint orders device-init work and any
     * backend-side submission that did not carry an application fence.
     *
     * B39: in pipelined mode there can be several batches outstanding, so
     * drain every issued slot first. The trailing finish_batch then adds its
     * own EOP checkpoint, which also covers work submitted outside the ring
     * (device init) exactly as B38 did. */
    vk_ps4_log_raw("QueueWaitIdle: submit completion checkpoint");
    /* Held across the drain so a concurrent submit cannot issue new work into
     * the ring while this call is proving the queue empty. */
    vk_ps4_queue_lock();
    VkResult result = vk_ps4_queue_drain(q->device);
    if (result != VK_SUCCESS) {
        vk_ps4_log("QueueWaitIdle: drain FAILED rc=%d", (int)result);
    } else {
        result = vk_ps4_queue_finish_batch(q->device, false, NULL);
        if (result != VK_SUCCESS) {
            vk_ps4_log("QueueWaitIdle: checkpoint FAILED rc=%d", (int)result);
        }
    }
    vk_ps4_queue_unlock();
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_DeviceWaitIdle(VkDevice device) {
    if (!device) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Device *dev = (VkPs4Device *)device;
    if (dev->gnm_device_lost) return VK_ERROR_DEVICE_LOST;
    if (dev->queue_count > 0 && dev->queues[0]) {
        return vk_ps4_QueueWaitIdle((VkQueue)dev->queues[0]);
    }
    /* A device may have no materialized VkQueue yet while its GNM init DCB is
     * still ordered on hardware.  The device-owned completion DCB remains
     * available, so use it directly and retain the same finite deadline. */
    vk_ps4_queue_lock();
    VkResult result = vk_ps4_queue_drain(dev);
    if (result == VK_SUCCESS) {
        result = vk_ps4_queue_finish_batch(dev, false, NULL);
    }
    vk_ps4_queue_unlock();
    return result;
}
