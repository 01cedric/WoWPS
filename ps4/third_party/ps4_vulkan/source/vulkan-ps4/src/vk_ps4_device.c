/*
 * vk_ps4_device.c — VkDevice / VkQueue implementation.
 *
 * On CreateDevice the device submits a one-shot GNM command buffer carrying
 * sceGnmDrawInitDefaultHardwareState so the GPU starts from a known context
 * before any Vulkan command buffer is submitted.  The backing store for that
 * preamble is held in VkPs4Device::gnm_init_mem and released on DestroyDevice
 * after the device is quiesced.
 */

#include "vk_ps4_internal.h"
#include "vk_ps4_clear_shader.h"

#include <stdlib.h>
#include <string.h>

#if defined(__ORBIS__)
/* Avoid including the legacy libkernel header here: its sched_param forward
 * declaration is incomplete under the strict OpenOrbis -Werror build. */
extern uint64_t sceKernelGetProcessTime(void);
extern int32_t sceKernelUsleep(uint32_t microseconds);
#endif

#ifdef VK_PS4_HAVE_PSBC
#include "psbc_compile.h"
#endif

/* Size of the device-wide GNM init command buffer, in dwords.
 * The default-hardware-state packet is 256 dwords (HW_INIT_PACKET_SIZE in
 * opengnm); 1024 dwords gives headroom for any future preamble additions. */
#define VK_PS4_GNM_INIT_CMD_DWORDS 1024u

/* Size of the epilogue command buffer used for EOP fence/semaphore writes.
 * An EVENT_WRITE_EOP packet is 6 dwords on GFX6-8; 64 dwords is plenty for
 * one EOP write plus NOP padding. */
#define VK_PS4_GNM_EPILOGUE_CMD_DWORDS 64u
#define VK_PS4_GNM_INIT_TIMEOUT_US 5000000ULL

/* Prove the init DCB has finished before any caller can fail initialization
 * and release its Garlic backing store.  A second ordered batch containing an
 * EOP label is sufficient: observing the label proves all earlier GNM work is
 * complete. */
static VkResult vk_ps4_device_wait_init_complete(VkPs4Device *dev) {
    if (!dev || !dev->gnm_epilogue_cmd ||
        dev->gnm_epilogue_cmd_dwords < 8) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    volatile uint32_t *label =
        (volatile uint32_t *)(dev->gnm_epilogue_cmd +
            dev->gnm_epilogue_cmd_dwords - 1);
    const uint32_t expected = 1u;
    *label = ~expected;

    GnmCommandBuffer completion = sceGnmCmdInit(
        dev->gnm_epilogue_cmd,
        dev->gnm_epilogue_cmd_dwords * sizeof(uint32_t), NULL, NULL
    );
    sceGnmDrawCmdEventWriteEop(
        &completion, GNM_CACHE_FLUSH_AND_INV_TS_EVENT,
        (uint64_t)(uintptr_t)label, GNM_DATA_SEL_SEND_DATA32,
        (uint64_t)expected
    );
    const uint32_t used_dwords =
        (uint32_t)(completion.cmdptr - completion.beginptr);
    if (used_dwords == 0 ||
        used_dwords >= dev->gnm_epilogue_cmd_dwords - 1) {
        vk_ps4_log_raw("init_gnm: completion EOP overflow/invalid");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    __asm__ volatile("sfence" ::: "memory");
#endif
    uint32_t used_bytes = used_dwords * sizeof(uint32_t);
    void *address = dev->gnm_epilogue_cmd;
    int32_t result = sceGnmSubmitCommandBuffers(
        1, &address, &used_bytes, NULL, NULL
    );
    if (result != 0) {
        vk_ps4_log("init_gnm: completion DCB submit FAILED: %d", result);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    result = sceGnmSubmitDone();
    if (result != 0) {
        vk_ps4_log("init_gnm: completion SubmitDone FAILED: %d", result);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

#if defined(__ORBIS__)
    const uint64_t start = sceKernelGetProcessTime();
    while (*label != expected) {
        if (sceKernelGetProcessTime() - start >=
            VK_PS4_GNM_INIT_TIMEOUT_US) {
            vk_ps4_log("init_gnm: completion TIMEOUT observed=%u",
                       (unsigned)*label);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        sceKernelUsleep(50);
    }
#else
    /* Generic GNM submits are no-ops in host contract builds. */
    *label = expected;
#endif
    dev->gnm_completion_value = expected;
    vk_ps4_log_raw("init_gnm: GPU completion checkpoint OK");
    return VK_SUCCESS;
}

/* Submit the GNM default-hardware-state preamble and mark the device as
 * GNM-initialized.  Returns VK_SUCCESS on success, an error otherwise.
 * On failure the caller is responsible for tearing down partial state. */
static VkResult vk_ps4_device_init_gnm(VkPs4Device *dev) {
    const uint64_t cmd_bytes = (uint64_t)VK_PS4_GNM_INIT_CMD_DWORDS * sizeof(uint32_t);
    const uint64_t alignment = 64 * 1024; /* 64KB Garlic alignment */

    vk_ps4_log_raw("init_gnm: allocating Garlic direct memory");
    GnmError err = sceGnmDirectMemoryAllocate(
        &dev->gnm_init_mem, cmd_bytes, alignment,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
    );
    if (err != GNM_ERROR_OK) {
        vk_ps4_log("init_gnm: DirectMemoryAllocate FAILED: %d", (int)err);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vk_ps4_log_raw("init_gnm: Garlic memory allocated OK");

    dev->gnm_init_cmd = (uint32_t *)dev->gnm_init_mem.mapped;
    dev->gnm_init_cmd_dwords = 0;

    /* Build the init command buffer in the mapped direct memory. */
    vk_ps4_log_raw("init_gnm: building init command buffer");
    GnmCommandBuffer cmd = sceGnmCmdInit(
        dev->gnm_init_cmd, (uint32_t)cmd_bytes, NULL, NULL
    );
    sceGnmDrawCmdInitDefaultHardwareState(&cmd);

    uint32_t used_dwords = (uint32_t)(cmd.cmdptr - cmd.beginptr);
    uint32_t used_bytes = used_dwords * sizeof(uint32_t);
    dev->gnm_init_cmd_dwords = used_dwords;
    vk_ps4_log("init_gnm: init CB built (%u dwords)", used_dwords);

    /* Submit the one-shot init packet.  On Orbis this programs the GPU's
     * default context state; on the host generic build it is a no-op. */
    vk_ps4_log_raw("init_gnm: submitting init command buffer");
    void *dcb_addr = dev->gnm_init_cmd;
    int32_t result = sceGnmSubmitCommandBuffers(
        1, &dcb_addr, &used_bytes, NULL, NULL
    );
    if (result != 0) {
        vk_ps4_log("init_gnm: SubmitCommandBuffers FAILED: %d", result);
        /* Once submission has been attempted, ownership is uncertain.  Do
         * not release the DCB backing store; Orbis process teardown will
         * reclaim it after CreateDevice reports failure. */
        dev->gnm_device_lost = true;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vk_ps4_log_raw("init_gnm: SubmitCommandBuffers OK");

    vk_ps4_log_raw("init_gnm: calling sceGnmSubmitDone");
    result = sceGnmSubmitDone();
    if (result != 0) {
        vk_ps4_log("init_gnm: SubmitDone FAILED: %d", result);
        /* The GPU may already be fetching the DCB.  Keep its memory alive
         * until process exit instead of racing an uncertain submission. */
        dev->gnm_device_lost = true;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vk_ps4_log_raw("init_gnm: SubmitDone OK");

    /* Allocate the epilogue command buffer used by QueueSubmit for EOP
         * fence/semaphore signal writes.  Small and reused across submits. */
    const uint64_t epilogue_bytes =
        (uint64_t)VK_PS4_GNM_EPILOGUE_CMD_DWORDS * sizeof(uint32_t);
    err = sceGnmDirectMemoryAllocate(
        &dev->gnm_epilogue_mem, epilogue_bytes, alignment,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
    );
    if (err != GNM_ERROR_OK) {
        /* The init DCB was already submitted.  Without an EOP allocation we
         * cannot prove completion, so deliberately retain its memory. */
        dev->gnm_device_lost = true;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    dev->gnm_epilogue_cmd = (uint32_t *)dev->gnm_epilogue_mem.mapped;
    dev->gnm_epilogue_cmd_dwords = VK_PS4_GNM_EPILOGUE_CMD_DWORDS;
    dev->gnm_completion_value = 0;
    dev->gnm_device_lost = false;

    /* B39: the completion slot ring that lets QueueSubmit return without
     * draining the GPU.  Allocated after the legacy epilogue buffer, so a
     * failure here only costs the pipelining - the serial path stays intact.
     * The init checkpoint below still uses the legacy buffer. */
    (void)vk_ps4_queue_init_completion_ring(dev);

    const VkResult init_completion =
        vk_ps4_device_wait_init_complete(dev);
    if (init_completion != VK_SUCCESS) {
        vk_ps4_log("init_gnm: GPU completion checkpoint FAILED: %d",
                   (int)init_completion);
        /* Never free potentially in-flight GPU memory on this path. */
        dev->gnm_device_lost = true;
        return init_completion;
    }

    /* Compile the clear with the same PSBC revision as application shaders.
     * B2's precompiled UBO shader advertised an indirect table at SGPR2, but
     * the emitter wrote an immediate V# at SGPR0. Its code also contained a
     * null descriptor generated without a descriptor layout. Do not execute
     * that stale binary. Compile with an explicit set0 UBO layout and bind
     * the resulting table pointer at the actual metadata register. */
#ifdef VK_PS4_HAVE_PSBC
    {
#include "../shaders/clear_spirv.h"
        PsbcCompileOptions options = vk_ps4_clear_compile_options(
            VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI);
        PsbcShaderOutput output = {0};
        const PsbcResult compiled = psbc_compile_shader(g_clear_spirv,
            sizeof(g_clear_spirv), &options, &output);
        GnmShaderMetadata meta = {0};
        const GnmError parsed = compiled == PSBC_RESULT_OK
            ? sceGnmShaderBinaryGetMetadata(output.data, output.size, &meta)
            : GNM_ERROR_INVALID_ARGS;
        vk_ps4_log("B3 clear PS compile: result=%u parse=%u binary=%u code=%u",
            (unsigned)compiled, (unsigned)parsed, (unsigned)output.size,
            parsed == GNM_ERROR_OK ? meta.shadercodesize : 0u);
        if (parsed == GNM_ERROR_OK && meta.type == GNM_SHADER_PIXEL &&
            meta.stage && meta.shadercode && meta.shadercodesize &&
            vk_ps4_clear_table_register((const GnmPsShader *)meta.stage,
                                        &dev->clear_ps_table_reg)) {
            const GnmPsShader *ps = (const GnmPsShader *)meta.stage;
            dev->clear_ps_regs = ps->registers;
            const GnmError allocated = sceGnmDirectMemoryAllocate(
                &dev->clear_ps_code_mem, meta.shadercodesize, 64u * 1024u,
                GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW);
            if (allocated == GNM_ERROR_OK && dev->clear_ps_code_mem.mapped) {
                memcpy(dev->clear_ps_code_mem.mapped, meta.shadercode, meta.shadercodesize);
                vk_ps4_cpu_store_fence();
                sceGnmPsRegsSetAddress(&dev->clear_ps_regs, dev->clear_ps_code_mem.mapped);
                dev->clear_ps_ready = true;
                vk_ps4_log("B3 clear PS: UBO code=%p bytes=%u set0-table-reg=%u address-hi=%u",
                    dev->clear_ps_code_mem.mapped, meta.shadercodesize,
                    dev->clear_ps_table_reg, VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI);
            }
        }
        psbc_free_output(&output);
    }
#endif
    if (!dev->clear_ps_ready) {
        vk_ps4_log_raw("B3 clear PS contract validation/upload failed; device creation stopped");
        return VK_ERROR_INVALID_SHADER_NV;
    }

#ifdef VK_PS4_HAVE_PSBC
    {
#include "../shaders/clear_fullscreen_spirv.h"
        PsbcCompileOptions opt = {0};
        opt.target = PSBC_TARGET_PS4_BASE;
        opt.stage = PSBC_STAGE_VERTEX;
        opt.entrypoint = "main";
        opt.optimise = true;
        PsbcShaderOutput output = {0};
        GnmShaderMetadata meta = {0};
        if (psbc_compile_shader(g_clear_fullscreen_spirv, sizeof(g_clear_fullscreen_spirv), &opt, &output) == PSBC_RESULT_OK &&
            sceGnmShaderBinaryGetMetadata(output.data, output.size, &meta) == GNM_ERROR_OK &&
            meta.type == GNM_SHADER_VERTEX && meta.stage && meta.shadercode && meta.shadercodesize) {
            const GnmVsShader *vs = (const GnmVsShader *)meta.stage;
            if (vs->numinputsemantics == 0 && vs->common.numinputusageslots == 0 &&
                sceGnmDirectMemoryAllocate(&dev->clear_vs_code_mem, meta.shadercodesize, 64u*1024u,
                    GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW) == GNM_ERROR_OK && dev->clear_vs_code_mem.mapped) {
                dev->clear_vs_regs = vs->registers;
                memcpy(dev->clear_vs_code_mem.mapped, meta.shadercode, meta.shadercodesize);
                vk_ps4_cpu_store_fence();
                sceGnmVsRegsSetAddress(&dev->clear_vs_regs, dev->clear_vs_code_mem.mapped);
                dev->clear_vs_ready = true;
                vk_ps4_log("B6 full-viewport clear VS: %u bytes, no resource arguments", meta.shadercodesize);
            }
        }
        psbc_free_output(&output);
    }
#endif
    if (!dev->clear_vs_ready) {
        vk_ps4_log_raw("B6 full-viewport clear VS initialization failed");
        return VK_ERROR_INVALID_SHADER_NV;
    }
    dev->gnm_initialized = true;
    return VK_SUCCESS;
}

/* Release the GNM init and epilogue command buffer backing stores.
 * Safe to call when gnm_initialized is false (no-op). */
static void vk_ps4_device_finish_gnm(VkPs4Device *dev) {
    if (dev->clear_vs_code_mem.allocated) sceGnmDirectMemoryRelease(&dev->clear_vs_code_mem);
    memset(&dev->clear_vs_code_mem, 0, sizeof(dev->clear_vs_code_mem));
    dev->clear_vs_ready = false;
    if (dev->clear_ps_code_mem.allocated) {
        sceGnmDirectMemoryRelease(&dev->clear_ps_code_mem);
    }
    memset(&dev->clear_ps_code_mem, 0, sizeof(dev->clear_ps_code_mem));
    dev->clear_ps_binary = NULL;
    dev->clear_ps_ready = false;
    /* B39: the ring's DCBs are GPU-fetchable memory like the epilogue buffer.
     * This function only runs after the caller has proved the GPU is idle
     * (the same precondition the epilogue release below already relies on). */
    vk_ps4_queue_release_completion_ring(dev);
    if (dev->gnm_epilogue_mem.allocated) {
        sceGnmDirectMemoryRelease(&dev->gnm_epilogue_mem);
    }
    dev->gnm_epilogue_cmd = NULL;
    dev->gnm_epilogue_cmd_dwords = 0;
    dev->gnm_completion_value = 0;
    if (dev->gnm_init_mem.allocated) {
        sceGnmDirectMemoryRelease(&dev->gnm_init_mem);
    }
    dev->gnm_init_cmd = NULL;
    dev->gnm_init_cmd_dwords = 0;
    dev->gnm_initialized = false;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDevice *pDevice
) {
    VK_PS4_LOG_ENTRY();
    if (!physicalDevice || !pCreateInfo || !pDevice) {
        vk_ps4_log_raw("CreateDevice: NULL args, FAIL");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;

    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &phys->instance->allocator;

    VkPs4Device *dev = vk_ps4_alloc_zero(alloc, sizeof(*dev), 16);
    if (!dev) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    dev->type = VK_PS4_OBJ_DEVICE;
    dev->physical_device = phys;
    if (pAllocator) {
        dev->allocator = *pAllocator;
    } else {
        /* Inherit instance allocator so destroy uses the same one */
        dev->allocator = phys->instance->allocator;
    }
    dev->gnm_initialized = false;

#ifdef VK_PS4_HAVE_PSBC
    /* Initialize libpsbc once per device — refcounted internally.
     * This avoids calling psbc_init/psbc_shutdown on every shader compile. */
    psbc_init();
    vk_ps4_log_raw("CreateDevice: psbc_init done");
#endif

    /* Submit the GNM default-hardware-state preamble so the GPU is in a
     * known state before any Vulkan command buffer is submitted.  This
     * replaces the previous "TODO: init GNM on PS4" stub. */
    vk_ps4_log_raw("CreateDevice: calling vk_ps4_device_init_gnm");
    VkResult gnm_result = vk_ps4_device_init_gnm(dev);
    if (gnm_result != VK_SUCCESS) {
        vk_ps4_log("CreateDevice: gnm init FAILED: %d", (int)gnm_result);
#ifdef VK_PS4_HAVE_PSBC
        psbc_shutdown();
#endif
        vk_ps4_free(alloc, dev);
        return gnm_result;
    }
    vk_ps4_log_raw("CreateDevice: GNM init OK");

    /* Pre-allocate queues from pCreateInfo so handles are stable */
    dev->queue_count = 0;
    for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
        const VkDeviceQueueCreateInfo *qci = &pCreateInfo->pQueueCreateInfos[i];
        for (uint32_t j = 0; j < qci->queueCount && dev->queue_count < VK_PS4_MAX_QUEUES; j++) {
            VkPs4Queue *queue = vk_ps4_alloc_zero(alloc, sizeof(*queue), 16);
            if (!queue) {
                /* Free already-allocated queues */
                for (uint32_t k = 0; k < dev->queue_count; k++) {
                    vk_ps4_free(alloc, dev->queues[k]);
                }
                vk_ps4_device_finish_gnm(dev);
#ifdef VK_PS4_HAVE_PSBC
                psbc_shutdown();
#endif
                vk_ps4_free(alloc, dev);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            queue->type = VK_PS4_OBJ_QUEUE;
            queue->device = dev;
            queue->family_index = qci->queueFamilyIndex;
            /* Set queue flags based on family index */
            if (qci->queueFamilyIndex == VK_PS4_QUEUE_FAMILY_GRAPHICS) {
                queue->flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                               VK_QUEUE_TRANSFER_BIT;
            } else if (qci->queueFamilyIndex == VK_PS4_QUEUE_FAMILY_COMPUTE) {
                queue->flags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
            } else {
                queue->flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                               VK_QUEUE_TRANSFER_BIT;
            }
            dev->queues[dev->queue_count++] = queue;
        }
    }

    *pDevice = (VkDevice)dev;
    vk_ps4_log("CreateDevice: OK (queues=%u)", dev->queue_count);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
    if (!device) {
        return;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    if (dev->gnm_device_lost || dev->gnm_present_in_progress ||
        dev->gnm_present_uncertain) {
        /* DestroyDevice is void, so there is no safe way to surface an
         * ownership error.  Never unmap PM4, shader or VideoOut-adjacent
         * direct memory while the GPU/display engine may still reference it;
         * Orbis process teardown reclaims it atomically. */
        vk_ps4_log("DestroyDevice: retained unsafe GNM state lost=%u present=%u uncertain=%u",
                   (unsigned)dev->gnm_device_lost,
                   (unsigned)dev->gnm_present_in_progress,
                   (unsigned)dev->gnm_present_uncertain);
        return;
    }
    /* Tear down GNM state — release the init command buffer backing store
     * after the device is quiesced.  The GPU has no outstanding work at this
     * point because all queues were idle when the app called DestroyDevice. */
    vk_ps4_device_finish_gnm(dev);
    /* Free cached queues */
    for (uint32_t i = 0; i < dev->queue_count; i++) {
        if (dev->queues[i]) {
            vk_ps4_free(alloc, dev->queues[i]);
            dev->queues[i] = NULL;
        }
    }
    dev->queue_count = 0;
#ifdef VK_PS4_HAVE_PSBC
    psbc_shutdown();
#endif
    vk_ps4_free(alloc, dev);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetDeviceQueue(
    VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue
) {
    if (!device || !pQueue) {
        return;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    /* Return the cached queue handle (stable across calls) */
    uint32_t idx = 0;
    for (uint32_t i = 0; i < dev->queue_count; i++) {
        if (dev->queues[i] && dev->queues[i]->family_index == queueFamilyIndex) {
            if (idx == queueIndex) {
                *pQueue = (VkQueue)dev->queues[i];
                return;
            }
            idx++;
        }
    }
    *pQueue = VK_NULL_HANDLE;
}
