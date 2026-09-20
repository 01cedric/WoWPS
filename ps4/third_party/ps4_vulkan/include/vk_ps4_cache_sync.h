#ifndef VK_PS4_CACHE_SYNC_H
#define VK_PS4_CACHE_SYNC_H

#include "gnm_drawcommandbuffer.h"
#include "pm4/amdgfxregs.h"

/* The pinned emitter rejects a NULL gpuaddr even for DATA_SEL_DISCARD and
 * emits no packet. Every active DCB is mapped GPU memory, retained until its
 * submission retires. Its base is therefore a valid, aligned address here;
 * DISCARD performs no memory write and cannot overwrite command words. */
static inline void vk_ps4_emit_cache_release(GnmCommandBuffer *cmd,
                                            GnmEventType event) {
    sceGnmDrawCmdEventWriteEop(cmd, event,
        (uint64_t)(uintptr_t)cmd->beginptr, GNM_DATA_SEL_DISCARD, 0);
}

static inline void vk_ps4_acquire_graphics_writes(GnmCommandBuffer *cmd,
                                                  bool shader_reads) {
    uint32_t flags = GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_CB1 |
        GNM_ACQUIRE_TARGET_CB2 | GNM_ACQUIRE_TARGET_CB3 |
        GNM_ACQUIRE_TARGET_CB4 | GNM_ACQUIRE_TARGET_CB5 |
        GNM_ACQUIRE_TARGET_CB6 | GNM_ACQUIRE_TARGET_CB7 | GNM_ACQUIRE_TARGET_DB;
    if (shader_reads) {
        /* WaitGraphicsWrite alone requests only volatile TC/TCL1 operations.
         * Rendered images are sampled through READONLY T# descriptors: their
         * previously cached lines must be invalidated as well. Keep the CB/DB
         * completion/flush targets, then acquire the complete shader caches. */
        flags |= S_0301F0_TC_ACTION_ENA(1) | S_0301F0_TCL1_ACTION_ENA(1) |
                 S_0301F0_SH_KCACHE_ACTION_ENA(1);
    }
    sceGnmDrawCmdWaitGraphicsWrite(cmd, (GnmAcquireTargetFlags)flags);
}

#endif
