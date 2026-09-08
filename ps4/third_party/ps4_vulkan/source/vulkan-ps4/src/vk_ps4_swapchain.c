/*
 * vk_ps4_swapchain.c — VkSwapchainKHR implementation via GnmVideoOut.
 *
 * vkCreateSwapchainKHR opens a GnmVideoOut, allocates display buffers,
 * and creates VkImage wrappers for each buffer.
 * vkAcquireNextImageKHR returns the next available buffer.
 * vkQueuePresentKHR submits the command buffer with a flip command.
 */

#include "vk_ps4_internal.h"
#include "vk_ps4.h"

#include <stdlib.h>
#include <string.h>

#if defined(__ORBIS__)
#include <orbis/VideoOut.h>
/* Including the legacy libkernel umbrella header breaks strict -Werror builds
 * through its incomplete sched_param declaration.  The concrete kernel types
 * come from VideoOut.h; declare only the wait entry point needed here. */
extern int32_t sceKernelWaitEqueue(OrbisKernelEqueue, OrbisKernelEvent *,
                                   int32_t, int32_t *, OrbisKernelUseconds *);
extern uint64_t sceKernelGetProcessTime(void);
extern int32_t sceKernelUsleep(uint32_t microseconds);
#endif

#define VK_PS4_VIDEO_OUT_FLIP_TIMEOUT_US 3000000u

static uint64_t vk_ps4_present_clock(void) {
#if defined(__ORBIS__)
    return sceKernelGetProcessTime();
#else
    return 0;
#endif
}

/* Pixel probes are diagnostic readbacks, not part of a normal frame. */
static bool vk_ps4_pixel_probes_enabled(void) {
    const char *value = getenv("WOWEE_VK_PIXEL_PROBES");
    return value && value[0] == '1';
}


/* The OpenGNM helper's default wait passes a NULL timeout and can block
 * forever if FW 5.05 accepts a flip but never posts its event.  Keep the same
 * ABI/state bookkeeping locally while enforcing a finite deadline. */
static GnmError vk_ps4_video_out_flip_bounded(
    GnmVideoOut *video_out, uint32_t buffer_index, int64_t flip_arg,
    int32_t flip_mode
) {
    if (!video_out || video_out->handle < 0 ||
        buffer_index >= video_out->numbuffers) {
        return GNM_ERROR_INVALID_ARGS;
    }
#if defined(__ORBIS__)
    video_out->last_error_stage = 7;
    int32_t result = sceVideoOutSubmitFlip(
        video_out->handle, (int32_t)buffer_index,
        (uint32_t)flip_mode, flip_arg
    );
    if (result != 0) {
        video_out->last_error_code = result;
        return (GnmError)result;
    }
    /* VideoOutOpen normally creates the event queue.  Treat a missing queue
     * as an ownership failure, never as a successful asynchronous present. */
    if (!video_out->flipqueue) {
        video_out->last_error_stage = 8;
        video_out->last_error_code = GNM_ERROR_INTERNAL_FAILURE;
        return GNM_ERROR_INTERNAL_FAILURE;
    }

    video_out->last_error_stage = 8;
    OrbisKernelEvent event;
    memset(&event, 0, sizeof(event));
    int32_t out = 0;
    OrbisKernelUseconds timeout = VK_PS4_VIDEO_OUT_FLIP_TIMEOUT_US;
    result = sceKernelWaitEqueue(
        (OrbisKernelEqueue)video_out->flipqueue,
        &event, 1, &out, &timeout
    );
    if (result != 0 || out != 1) {
        video_out->last_error_code = result != 0
            ? result : GNM_ERROR_INTERNAL_FAILURE;
        return result != 0
            ? (GnmError)result : GNM_ERROR_INTERNAL_FAILURE;
    }

    /* The event is the first ownership boundary, but on FW 5.05 also prove
     * that VideoOut no longer reports a pending flip before exposing success.
     * A short bounded poll absorbs the event/status publication race. */
    const uint64_t pending_start = sceKernelGetProcessTime();
    for (;;) {
        const int32_t pending = sceVideoOutIsFlipPending(video_out->handle);
        if (pending == 0) break;
        if (pending < 0 ||
            sceKernelGetProcessTime() - pending_start >=
                VK_PS4_VIDEO_OUT_FLIP_TIMEOUT_US) {
            video_out->last_error_code = pending < 0
                ? pending : GNM_ERROR_INTERNAL_FAILURE;
            return pending < 0
                ? (GnmError)pending : GNM_ERROR_INTERNAL_FAILURE;
        }
        sceKernelUsleep(50);
    }
    video_out->frame += 1;
    video_out->currentbuffer =
        (buffer_index + 1) % video_out->numbuffers;
    video_out->last_error_stage = 0;
    video_out->last_error_code = GNM_ERROR_OK;
    return GNM_ERROR_OK;
#else
    return sceGnmVideoOutSubmitFlipAndWait(
        video_out, buffer_index, flip_arg, flip_mode
    );
#endif
}

/* === B39 asynchronous present ===
 *
 * The blocking helper above is what B38 called for every frame: submit the
 * flip, then wait for its VideoOut event and for IsFlipPending to clear. The
 * console has several scanout buffers, so that wait is not needed to be
 * correct - it only has to happen before a buffer is drawn into again. Moving
 * it there takes a whole scanout interval off the frame's critical path.
 *
 * WOWEE_VK_ASYNC_FLIP=0 restores the blocking present. */
static bool vk_ps4_async_flip_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("WOWEE_VK_ASYNC_FLIP");
        cached = (v && *v == '0') ? 0 : 1;
    }
    return cached != 0;
}

/* Submit a flip without waiting for its event. */
static GnmError vk_ps4_video_out_flip_submit(
    GnmVideoOut *video_out, uint32_t buffer_index, int64_t flip_arg,
    int32_t flip_mode
) {
    if (!video_out || video_out->handle < 0 ||
        buffer_index >= video_out->numbuffers) {
        return GNM_ERROR_INVALID_ARGS;
    }
#if defined(__ORBIS__)
    video_out->last_error_stage = 7;
    const int32_t result = sceVideoOutSubmitFlip(
        video_out->handle, (int32_t)buffer_index,
        (uint32_t)flip_mode, flip_arg
    );
    if (result != 0) {
        video_out->last_error_code = result;
        return (GnmError)result;
    }
    /* VideoOutOpen normally creates the event queue. Without it there is no
     * way to observe completion later, so refuse rather than report an
     * asynchronous present nothing can ever retire. */
    if (!video_out->flipqueue) {
        video_out->last_error_stage = 8;
        video_out->last_error_code = GNM_ERROR_INTERNAL_FAILURE;
        return GNM_ERROR_INTERNAL_FAILURE;
    }
    video_out->frame += 1;
    video_out->currentbuffer = (buffer_index + 1) % video_out->numbuffers;
    video_out->last_error_stage = 0;
    video_out->last_error_code = GNM_ERROR_OK;
    return GNM_ERROR_OK;
#else
    (void)flip_arg;
    (void)flip_mode;
    video_out->frame += 1;
    video_out->currentbuffer = (buffer_index + 1) % video_out->numbuffers;
    return GNM_ERROR_OK;
#endif
}

/* Collect one flip event.  Returns false only on a real failure; a
 * non-blocking call that found no event returns true with *out_reaped false. */
static bool vk_ps4_video_out_reap_flip(
    VkPs4Swapchain *sc, bool block, bool *out_reaped
) {
    if (out_reaped) *out_reaped = false;
    if (!sc || sc->pending_flip_count == 0) return true;
#if defined(__ORBIS__)
    OrbisKernelEvent event;
    memset(&event, 0, sizeof(event));
    int32_t out = 0;
    OrbisKernelUseconds timeout = block ? VK_PS4_VIDEO_OUT_FLIP_TIMEOUT_US : 0;
    const int32_t result = sceKernelWaitEqueue(
        (OrbisKernelEqueue)sc->video_out.flipqueue, &event, 1, &out, &timeout
    );
    if (result != 0 || out != 1) {
        if (!block) return true;   /* nothing queued yet, and none required */
        sc->video_out.last_error_code =
            result != 0 ? result : GNM_ERROR_INTERNAL_FAILURE;
        return false;
    }
#else
    /* Generic backend (host tests only): model the hardware conservatively.
     * A non-blocking check finds nothing, because nothing has told us a flip
     * completed; a blocking wait retires exactly one. That keeps the FIFO and
     * ownership bookkeeping below on the same code path the console takes. */
    if (!block) return true;
#endif
    /* The oldest submitted flip is now on screen. The buffer it replaced is
     * the one that becomes reusable - the newly displayed one is being
     * scanned out and must stay untouched. */
    const uint32_t now_displayed = sc->pending_flips[0];
    for (uint32_t i = 1; i < sc->pending_flip_count; ++i) {
        sc->pending_flips[i - 1] = sc->pending_flips[i];
    }
    --sc->pending_flip_count;

    if (sc->displayed_image < sc->image_count) {
        sc->image_in_flight[sc->displayed_image] = false;
        sc->image_fences[sc->displayed_image] = NULL;
    }
    sc->displayed_image = now_displayed;
    if (sc->pending_flip_count == 0) {
        sc->flip_submission_uncertain = false;
        sc->last_present_confirmed = true;
        if (sc->device) sc->device->gnm_present_in_progress = false;
    }
    if (out_reaped) *out_reaped = true;
    return true;
}

/* Drain every outstanding flip.  Used before teardown and by the acquire
 * fallback that needs the display engine to have released everything. */
static bool vk_ps4_video_out_drain_flips(VkPs4Swapchain *sc) {
    if (!sc) return true;
    while (sc->pending_flip_count > 0) {
        bool reaped = false;
        if (!vk_ps4_video_out_reap_flip(sc, true, &reaped)) return false;
        if (!reaped) return false;
    }
    return true;
}

/*
 * Public, narrow bridge for CPU compositors.  The application never sees the
 * VkPs4Swapchain layout: this translation unit alone validates the opaque
 * Vulkan handle, the acquisition state and the VideoOut-backed allocation.
 *
 * This query deliberately does not make a display-engine safety claim.  The
 * M65 presenter performs its existing submitted-and-waited
 * WaitUntilSafeForRendering boundary before it writes through this pointer.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vkPs4GetAcquiredScanoutInfo(
    VkSwapchainKHR swapchain, uint32_t image_index,
    VkPs4ScanoutInfo *p_info
) {
    if (!p_info) return VK_ERROR_INITIALIZATION_FAILED;
    memset(p_info, 0, sizeof(*p_info));
    if (!swapchain) return VK_ERROR_INITIALIZATION_FAILED;

    VkPs4Swapchain *sc = (VkPs4Swapchain *)swapchain;
    if (sc->type != VK_PS4_OBJ_SWAPCHAIN_KHR || !sc->images ||
        sc->video_out.handle < 0 || image_index >= sc->image_count) {
        return VK_ERROR_SURFACE_LOST_KHR;
    }
    if (!sc->image_in_flight[image_index]) {
        return VK_NOT_READY;
    }
    /* The caller must have acquired this image, and the display engine must
     * not be reading it.
     *
     * B39: flip_submission_uncertain is no longer a whole-swapchain verdict -
     * with asynchronous present there is normally a flip outstanding for some
     * other buffer while this one is being drawn into, which says nothing
     * about this index. Ask about this buffer specifically instead, so the
     * query answers the question it is named for. */
    if (image_index == sc->displayed_image) return VK_NOT_READY;
    for (uint32_t i = 0; i < sc->pending_flip_count; ++i) {
        if (sc->pending_flips[i] == image_index) return VK_NOT_READY;
    }
    if (sc->pending_flip_count == 0 && sc->flip_submission_uncertain) {
        /* A submit failed somewhere between the syscall and its event: the
         * old all-or-nothing latch is the only thing left to trust. */
        return VK_NOT_READY;
    }

    VkPs4Image *image = &sc->images[image_index];
    const uint32_t width = image->create_info.extent.width;
    const uint32_t height = image->create_info.extent.height;
    uint32_t bytes_per_pixel = 0;
    switch (image->create_info.format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
        bytes_per_pixel = 4;
        break;
    default:
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    VkPs4DeviceMemory *memory = &sc->image_memory[image_index];
    void *pixels = memory->gnm_mem.mapped;
    const uint32_t pitch_pixels = sceGnmRtGetPitch(&image->gnm_rt);
    if (!pixels || image->memory != memory || pitch_pixels == 0 ||
        pitch_pixels > UINT32_MAX / bytes_per_pixel) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const uint32_t row_pitch = pitch_pixels * bytes_per_pixel;
    const uint64_t required_bytes =
        (uint64_t)row_pitch * (uint64_t)height;
    if (width == 0 || height == 0 || pitch_pixels < width ||
        required_bytes > memory->size) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    p_info->pixels = pixels;
    p_info->byteSize = memory->size;
    p_info->width = width;
    p_info->height = height;
    p_info->rowPitchBytes = row_pitch;
    p_info->bytesPerPixel = bytes_per_pixel;
    p_info->vkFormat = image->create_info.format;
    p_info->nativeVideoOutFormat = sc->video_out.registered_pixel_format;
    p_info->imageIndex = image_index;
    p_info->acquired = VK_TRUE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vkPs4GetSwapchainImageMemory(
    VkSwapchainKHR swapchain, uint32_t image_index,
    VkDeviceMemory *p_memory
) {
    if (!p_memory) return VK_ERROR_INITIALIZATION_FAILED;
    *p_memory = VK_NULL_HANDLE;
    if (!swapchain) return VK_ERROR_INITIALIZATION_FAILED;

    VkPs4Swapchain *sc = (VkPs4Swapchain *)swapchain;
    if (sc->type != VK_PS4_OBJ_SWAPCHAIN_KHR || !sc->images ||
        image_index >= sc->image_count) {
        return VK_ERROR_SURFACE_LOST_KHR;
    }

    VkPs4DeviceMemory *memory = &sc->image_memory[image_index];
    if (!memory->gnm_mem.mapped) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    *p_memory = (VkDeviceMemory)memory;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
    VK_PS4_LOG_ENTRY();
    if (!device || !pCreateInfo || !pSwapchain) {
        vk_ps4_log_raw("CreateSwapchainKHR: NULL args, FAIL");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Swapchain *sc = vk_ps4_alloc_zero(alloc, sizeof(*sc), 16);
    if (!sc) return VK_ERROR_OUT_OF_HOST_MEMORY;
    sc->type = VK_PS4_OBJ_SWAPCHAIN_KHR;
    sc->device = dev;
    sc->create_info = *pCreateInfo;  /* shallow copy — see below for pointer fixup */
    sc->current_image = 0;

    /* Validate minImageCount */
    if (pCreateInfo->minImageCount == 0) {
        vk_ps4_free(alloc, sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Null out dangling pointers in create_info — we don't use them after creation */
    sc->create_info.pNext = NULL;
    sc->create_info.pQueueFamilyIndices = NULL;
    sc->create_info.oldSwapchain = VK_NULL_HANDLE;

    /* Initialize VideoOut */
    GnmVideoOutCreateInfo vo_info;
    uint32_t width = pCreateInfo->imageExtent.width;
    uint32_t height = pCreateInfo->imageExtent.height;
    if (width == 0 || height == 0) {
        width = GNM_VIDEO_OUT_DEFAULT_WIDTH;
        height = GNM_VIDEO_OUT_DEFAULT_HEIGHT;
    }
    sceGnmVideoOutInitDefaultCreateInfo(&vo_info, width, height);

    /* Tried forcing tiling_mode to a guessed "tiled" value here so VideoOut
     * would size/align its buffers for a tiled render target and detile on
     * scanout. That guess (0, by analogy with the real SCE_VIDEO_OUT_TILING
     * _MODE enum) made sceGnmVideoOutOpen itself fail with
     * ORBIS_VIDEO_OUT_ERROR_SLOT_OCCUPIED (0x80290010) - opengnm's actual
     * field semantics here aren't documented and evidently aren't that
     * simple, so leave this at its LINEAR default (the one path proven to
     * open successfully) and try a lighter tiled mode for the render target
     * itself instead - see the sceGnmRtCreateColorTarget call below. */

    /* Set number of buffers from swapchain create info */
    vo_info.numbuffers = pCreateInfo->minImageCount;
    if (vo_info.numbuffers > GNM_VIDEO_OUT_MAX_BUFFERS) {
        vo_info.numbuffers = GNM_VIDEO_OUT_MAX_BUFFERS;
    }
    if (vo_info.numbuffers == 0) {
        vk_ps4_free(alloc, sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Open VideoOut */
    vk_ps4_log("CreateSwapchainKHR: VideoOutOpen %ux%u buffers=%u",
               width, height, vo_info.numbuffers);
    GnmError err = sceGnmVideoOutOpen(&sc->video_out, &vo_info);
    if (err != GNM_ERROR_OK) {
        vk_ps4_log("CreateSwapchainKHR: VideoOutOpen FAILED rc=%d stage=%u detail=%d",
                   (int)err, sc->video_out.last_error_stage,
                   sc->video_out.last_error_code);
        vk_ps4_free(alloc, sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vk_ps4_log("CreateSwapchainKHR: VideoOutOpen OK handle=%d pitch=%u format=0x%08x buffer=%llu stride=%llu",
               sc->video_out.handle, sc->video_out.pitch,
               sc->video_out.registered_pixel_format,
               (unsigned long long)sc->video_out.buffersize,
               (unsigned long long)sc->video_out.bufferstride);

    /* Create VkImage wrappers for each VideoOut buffer */
    sc->image_count = vo_info.numbuffers;
    /* B39: no flip has been submitted yet, so nothing is on screen. Marking
     * displayed_image out of range keeps the first reap from freeing a buffer
     * that was never presented. */
    sc->pending_flip_count = 0;
    sc->displayed_image = sc->image_count;
    sc->async_flip_enabled = vk_ps4_async_flip_enabled();
    vk_ps4_log("B39 present: async flip %s, %u scanout buffers",
               sc->async_flip_enabled ? "enabled" : "disabled (blocking)",
               sc->image_count);
    sc->images = vk_ps4_alloc_zero(alloc, sc->image_count * sizeof(VkPs4Image), 16);
    if (!sc->images) {
        sceGnmVideoOutClose(&sc->video_out);
        vk_ps4_free(alloc, sc);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    bool rt_ok = true;
    for (uint32_t i = 0; i < sc->image_count; i++) {
        VkPs4Image *img = &sc->images[i];
        img->type = VK_PS4_OBJ_IMAGE;
        img->device = dev;
        memset(&img->create_info, 0, sizeof(img->create_info));
        img->create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img->create_info.imageType = VK_IMAGE_TYPE_2D;
        img->create_info.format = pCreateInfo->imageFormat;
        img->create_info.extent.width = width;
        img->create_info.extent.height = height;
        img->create_info.extent.depth = 1;
        img->create_info.mipLevels = 1;
        img->create_info.arrayLayers = 1;
        img->create_info.samples = VK_SAMPLE_COUNT_1_BIT;
        img->create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        img->create_info.usage = pCreateInfo->imageUsage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        img->create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img->is_render_target = true;
        img->layout = VK_IMAGE_LAYOUT_UNDEFINED;
        /* Mark as swapchain image for WaitUntilSafeForRendering. */
        img->is_swapchain_image = true;
        img->video_out_handle = sc->video_out.handle;
        img->swapchain_buffer_index = i;

        /* Set up render target descriptor pointing to the VideoOut buffer */
        void *buffer = sceGnmVideoOutGetBuffer(&sc->video_out, i);
        if (!buffer) {
            vk_ps4_log("CreateSwapchainKHR: buffer %u is NULL", i);
            rt_ok = false;
            break;
        }
        vk_ps4_log("CreateSwapchainKHR: buffer[%u]=%p size=%llu",
                   i, buffer,
                   (unsigned long long)sc->video_out.buffersize);
        VkPs4DeviceMemory *display_mem = &sc->image_memory[i];
        memset(display_mem, 0, sizeof(*display_mem));
        display_mem->type = VK_PS4_OBJ_DEVICE_MEMORY;
        display_mem->device = dev;
        display_mem->memory_type_index = VK_PS4_MEMORY_TYPE_GARLIC;
        display_mem->size = sc->video_out.buffersize;
        display_mem->gnm_mem.mapped = buffer;
        display_mem->gnm_mem.size = sc->video_out.buffersize;
        display_mem->gnm_mem.alignment = GNM_VIDEO_OUT_MEMORY_ALIGNMENT;
        display_mem->gnm_mem.allocated = false;
        img->memory = display_mem;
        img->memory_offset = 0;
        GnmDataFormat gnm_fmt = vk_ps4_vk_format_to_gnm(pCreateInfo->imageFormat);
        uint64_t rt_size = 0;
        uint32_t rt_align = 0;
        /* Reverted the DISPLAY_2D_THIN / DISPLAY_1D_THIN tiled-render-target
         * experiments: switching tile mode also switches vk_ps4_clear_color()
         * from a FillMemory clear to a draw-based one, and the pixel-dump
         * diagnostic (QueuePresent, below) assumes simple row-major linear
         * addressing - which is wrong for any tiled surface, since real pixel
         * data lives at swizzled tile addresses. The "draws still don't show
         * up" result from that run isn't trustworthy: the diagnostic may
         * simply have been reading the wrong bytes. Back to
         * DISPLAY_LINEAR_GENERAL, where the diagnostic's addressing is valid
         * and the "FillMemory writes land, CB-exported draws don't" result is
         * real. See the EOP flush change in vk_ps4_CmdEndRenderPass for the
         * next theory: a missing CB-specific cache flush before flip. */
        GnmError rt_err = sceGnmRtCreateColorTarget(
            &img->gnm_rt, buffer, gnm_fmt,
            width, height, 1, 1, 1,
            GNM_TM_DISPLAY_LINEAR_GENERAL, GNM_GPU_BASE,
            &rt_size, &rt_align
        );
        if (rt_err != GNM_ERROR_OK) {
            vk_ps4_log("CreateSwapchainKHR: RT[%u] create FAILED rc=%d",
                       i, (int)rt_err);
            rt_ok = false;
            break;
        }
        if (rt_size == 0 || rt_size > sc->video_out.bufferstride) {
            vk_ps4_log("CreateSwapchainKHR: RT[%u] invalid size=%llu capacity=%llu align=%u",
                       i, (unsigned long long)rt_size,
                       (unsigned long long)sc->video_out.bufferstride, rt_align);
            rt_ok = false;
            break;
        }
        vk_ps4_log("CreateSwapchainKHR: RT[%u] OK size=%llu align=%u",
                   i, (unsigned long long)rt_size, rt_align);
    }

    if (!rt_ok) {
        vk_ps4_free(alloc, sc->images);
        sceGnmVideoOutClose(&sc->video_out);
        vk_ps4_free(alloc, sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    *pSwapchain = (VkSwapchainKHR)sc;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator) {
    if (!device || !swapchain) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Swapchain *sc = (VkPs4Swapchain *)swapchain;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    /* Vulkan requires the caller to stop using presentation resources before
     * destroying the swapchain.  Do not hide another DeviceWaitIdle here:
     * the M6.0.1 backend already performs one checked, bounded idle checkpoint
     * before teardown.  A second internal checkpoint could fail after the
     * caller's successful one and then make this void function free VideoOut
     * memory whose ownership is uncertain. */

    /* B39: outstanding asynchronous flips are the one form of "still in use"
     * that can be resolved here rather than only reported. Draining them is
     * the same event wait the blocking present performed, so the ownership
     * test below reaches its normal state instead of retaining VideoOut for
     * the rest of the process on every clean shutdown. */
    if (sc->pending_flip_count > 0 && vk_ps4_video_out_drain_flips(sc)) {
        /* Every queued flip has reported. The buffer now on screen is the one
         * a running frame loop would keep reserved, but at teardown the
         * IsFlipPending check below is the authority on whether the display
         * engine is still mid-handover - which is exactly the proof the
         * blocking present used before it released the same buffer. */
        if (sc->displayed_image < sc->image_count) {
            sc->image_in_flight[sc->displayed_image] = false;
            sc->image_fences[sc->displayed_image] = NULL;
        }
    }

    bool acquired_or_unconfirmed =
        sc->flip_submission_uncertain || sc->pending_flip_count > 0;
    for (uint32_t i = 0; i < sc->image_count; i++) {
        if (sc->image_in_flight[i]) {
            acquired_or_unconfirmed = true;
            break;
        }
    }
    int32_t pending = 0;
#if defined(__ORBIS__)
    if (sc->video_out.handle >= 0) {
        pending = sceVideoOutIsFlipPending(sc->video_out.handle);
    }
#endif
    if (acquired_or_unconfirmed || pending != 0) {
        /* A void Vulkan destroy entry point cannot report this ownership
         * failure.  Retaining VideoOut and its direct memory until process
         * exit is safer than unregistering a buffer still owned by scanout. */
        sc->flip_submission_uncertain = true;
        dev->gnm_present_in_progress = true;
        dev->gnm_present_uncertain = true;
        vk_ps4_log("DestroySwapchainKHR: retained unsafe VideoOut acquired=%u pending=%d confirmed=%u",
                   (unsigned)acquired_or_unconfirmed, (int)pending,
                   (unsigned)sc->last_present_confirmed);
        return;
    }

    if (sc->images) {
        vk_ps4_free(alloc, sc->images);
    }
    sceGnmVideoOutClose(&sc->video_out);
    vk_ps4_free(alloc, sc);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
                             uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages) {
    if (!device || !swapchain || !pSwapchainImageCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Swapchain *sc = (VkPs4Swapchain *)swapchain;

    if (!pSwapchainImages) {
        *pSwapchainImageCount = sc->image_count;
        return VK_SUCCESS;
    }

    if (*pSwapchainImageCount < sc->image_count) {
        *pSwapchainImageCount = sc->image_count;
        return VK_INCOMPLETE;
    }

    for (uint32_t i = 0; i < sc->image_count; i++) {
        pSwapchainImages[i] = (VkImage)&sc->images[i];
    }
    *pSwapchainImageCount = sc->image_count;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                           VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex) {
    if (!swapchain || !pImageIndex) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Swapchain *sc = (VkPs4Swapchain *)swapchain;
    if (sc->image_count == 0) {
        return VK_ERROR_SURFACE_LOST_KHR;
    }

    /* B39: collect any flip that has already completed. This costs a
     * non-blocking equeue check and is what usually makes the search below
     * find a free buffer immediately - the previous frame's scanout normally
     * finished while the CPU was recording this one, which is the overlap the
     * blocking present used to give away. */
    for (;;) {
        bool reaped = false;
        if (!vk_ps4_video_out_reap_flip(sc, false, &reaped)) break;
        if (!reaped) break;
    }

    /* Find an available image.  An image is available if it is not
     * in-flight (image_in_flight[idx] == false).  If the caller
     * previously passed a fence, we check it as an optimization to
     * reclaim images whose GPU work has completed even before the
     * present call.  We search starting from current_image to
     * distribute load across buffers. */
    uint32_t acquired = sc->image_count;  /* invalid sentinel */
    for (uint32_t attempt = 0; attempt < sc->image_count; attempt++) {
        uint32_t idx = (sc->current_image + attempt) % sc->image_count;
        if (!sc->image_in_flight[idx]) {
            /* Image is free */
            acquired = idx;
            break;
        }
        /* Image is in-flight — check if its fence has been signaled.
         * If so, the GPU work is done and we can reclaim it early
         * (even before QueuePresentKHR clears the in-flight flag). */
        if (sc->image_fences[idx]) {
            VkPs4Fence *f = (VkPs4Fence *)sc->image_fences[idx];
            bool done = f->signaled;
            if (!done && f->label) {
                done = (*f->label == f->signal_value);
            }
            if (done) {
                sc->image_in_flight[idx] = false;
                sc->image_fences[idx] = NULL;
                acquired = idx;
                break;
            }
        }
    }

    /* Still nothing free, but a flip is queued: block for its event. This is
     * the wait B38 performed inside every present, now paid only when the
     * display engine genuinely has not released a buffer yet - which is also
     * where it doubles as the frame pacer. */
    if (acquired >= sc->image_count && sc->pending_flip_count > 0) {
        bool reaped = false;
        if (vk_ps4_video_out_reap_flip(sc, true, &reaped) && reaped) {
            for (uint32_t attempt = 0; attempt < sc->image_count; attempt++) {
                const uint32_t idx =
                    (sc->current_image + attempt) % sc->image_count;
                if (!sc->image_in_flight[idx]) {
                    acquired = idx;
                    break;
                }
            }
        }
    }

    if (acquired >= sc->image_count) {
        /* All images are in flight.  Wait for any one to become available.
         * Collect all in-flight fences and wait with waitAll=false so
         * we wait up to `timeout` for ANY fence, not the full timeout
         * per fence. */
        VkFence wait_fences[GNM_VIDEO_OUT_MAX_BUFFERS];
        uint32_t wait_count = 0;
        for (uint32_t i = 0; i < sc->image_count; i++) {
            if (sc->image_fences[i]) {
                wait_fences[wait_count++] = sc->image_fences[i];
            }
        }
        if (wait_count > 0) {
            VkResult wr = vk_ps4_WaitForFences(
                device, wait_count, wait_fences, VK_FALSE, timeout
            );
            if (wr != VK_SUCCESS) {
                return wr;  /* VK_TIMEOUT or error */
            }
            /* Find which fence signaled and reclaim its image. */
            for (uint32_t i = 0; i < sc->image_count; i++) {
                if (sc->image_fences[i]) {
                    VkPs4Fence *f = (VkPs4Fence *)sc->image_fences[i];
                    bool done = f->signaled;
                    if (!done && f->label) {
                        done = (*f->label == f->signal_value);
                    }
                    if (done) {
                        sc->image_in_flight[i] = false;
                        sc->image_fences[i] = NULL;
                        acquired = i;
                        break;
                    }
                }
            }
            if (acquired >= sc->image_count) {
                /* WaitForFences returned success but we didn't find
                 * a signaled fence — shouldn't happen, but handle it. */
                acquired = sc->current_image;
            }
        } else {
            /* No fences to wait on (all semaphore-only sync).
             * Wait for all GPU work to complete before reusing the
             * image, since we have no per-image fence to wait on.
             * This is conservative but correct — without it, the GPU
             * may still be rendering to the buffer we're about to
             * hand back to the caller. */
            VkResult idle_result = vk_ps4_DeviceWaitIdle(device);
            if (idle_result != VK_SUCCESS) {
                vk_ps4_log("AcquireNextImageKHR: device idle FAILED rc=%d",
                           (int)idle_result);
                return idle_result;
            }
            /* B39: a GPU idle says nothing about the display engine, which
             * owns its buffers independently. Drain the outstanding flips
             * before declaring every image reusable. */
            if (!vk_ps4_video_out_drain_flips(sc)) {
                vk_ps4_log("AcquireNextImageKHR: flip drain FAILED rc=%d",
                           sc->video_out.last_error_code);
                return VK_ERROR_SURFACE_LOST_KHR;
            }
            /* Clear all in-flight flags since the GPU is now idle and the
             * display engine has reported every queued flip. The buffer being
             * scanned out is still owned by VideoOut, so it stays reserved. */
            for (uint32_t i = 0; i < sc->image_count; i++) {
                if (i == sc->displayed_image) continue;
                sc->image_in_flight[i] = false;
            }
            acquired = sc->image_count;
            for (uint32_t attempt = 0; attempt < sc->image_count; attempt++) {
                const uint32_t idx =
                    (sc->current_image + attempt) % sc->image_count;
                if (!sc->image_in_flight[idx]) { acquired = idx; break; }
            }
            if (acquired >= sc->image_count) acquired = sc->current_image;
        }
    }

    /* Mark this image as in-flight */
    sc->image_in_flight[acquired] = true;
    sc->image_fences[acquired] = fence;  /* may be NULL */
    sc->current_image = (acquired + 1) % sc->image_count;
    *pImageIndex = acquired;

    /* Signal semaphore and fence.  Per Vulkan spec, the semaphore/fence
     * passed to AcquireNextImageKHR is signaled when the image is
     * available, which is now.  We only set the CPU-side signaled flag
     * — we do NOT increment signal_value or write the GPU label, because
     * QueueSubmit owns GPU-side signaling (EOP writes).  Touching
     * signal_value here would cause a double-increment if the caller
     * reuses the same fence/semaphore with QueueSubmit. */
    if (semaphore) {
        VkPs4Semaphore *sem = (VkPs4Semaphore *)semaphore;
        sem->signaled = true;
    }
    if (fence) {
        VkPs4Fence *f = (VkPs4Fence *)fence;
        f->signaled = true;
    }

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
    if (!queue || !pPresentInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pPresentInfo->pSwapchains || !pPresentInfo->pImageIndices) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Queue *q = (VkPs4Queue *)queue;
    const uint64_t present_start = vk_ps4_present_clock();

    /* Never race VideoOut against an unfinished render.
     *
     * B38 could simply test the semaphore, because QueueSubmit had already
     * drained the GPU before returning. B39 submits without waiting, so the
     * render batch is still in flight here - by design. This is the one place
     * the completion proof is genuinely required, so wait for the batch's
     * ticket rather than refusing the present. The same EOP is observed; it
     * is only observed here instead of inside every submit, which is what
     * lets uploads and the next frame's recording proceed unblocked. */
    for (uint32_t s = 0; s < pPresentInfo->waitSemaphoreCount; s++) {
        VkPs4Semaphore *sem = (VkPs4Semaphore *)pPresentInfo->pWaitSemaphores[s];
        if (sem) {
            if (sem->pending.slot_index != 0 && sem->device) {
                if (!vk_ps4_queue_ticket_wait(sem->device, &sem->pending)) {
                    vk_ps4_log("QueuePresent: render completion TIMEOUT sem=%u", s);
                    return VK_ERROR_DEVICE_LOST;
                }
                vk_ps4_sync_resolve_semaphore(sem);
            }
            bool ready = sem->signaled;
            if (!ready && sem->label && sem->signal_value != 0) {
                ready = (*sem->label == sem->signal_value);
            }
            if (!ready) {
                vk_ps4_log("QueuePresent: wait semaphore %u NOT_READY", s);
                return VK_NOT_READY;
            }
            sem->signaled = false;
        }
    }

    const uint64_t render_wait_us = vk_ps4_present_clock() - present_start;

    /* Submit flip for each swapchain.  After the flip completes
     * (sceGnmVideoOutSubmitFlipAndWait is blocking), the image is
     * no longer in flight — clear its fence so AcquireNextImageKHR
     * can reclaim it. */
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        VkPs4Swapchain *sc = (VkPs4Swapchain *)pPresentInfo->pSwapchains[i];
        if (!sc) {
            vk_ps4_log("QueuePresent: NULL swapchain index=%u", i);
            if (pPresentInfo->pResults) {
                pPresentInfo->pResults[i] = VK_ERROR_SURFACE_LOST_KHR;
            }
            return VK_ERROR_SURFACE_LOST_KHR;
        }
        uint32_t image_index = pPresentInfo->pImageIndices[i];

        if (image_index >= sc->image_count) {
            vk_ps4_log("QueuePresent: invalid image=%u count=%u index=%u",
                       image_index, sc->image_count, i);
            if (pPresentInfo->pResults) {
                pPresentInfo->pResults[i] = VK_ERROR_OUT_OF_DATE_KHR;
            }
            return VK_ERROR_OUT_OF_DATE_KHR;
        }

        /* Submit flip for this buffer.  Preserve every breadcrumb through the
         * 120-frame M6.0.1 probe, then rate-limit successful flip logging so
         * continuous world rendering does not flush the log twice per frame. */
        const uint64_t flip_serial = (uint64_t)sc->video_out.frame + 1u;
        const bool trace_success =
            flip_serial <= 128u || (flip_serial % 120u) == 0u;
        if (trace_success) {
            vk_ps4_log("QueuePresent: flip img=%u frame=%lld",
                       image_index, (long long)sc->video_out.frame);
        }
        /* Latch ownership before the syscall: a failure after SubmitFlip may
         * still leave VideoOut reading the buffer.  Only a flip event (plus,
         * on the blocking path, a no-pending proof) may clear it. */
        sc->flip_submission_uncertain = true;
        sc->last_present_confirmed = false;
        if (q->device) q->device->gnm_present_in_progress = true;

        /* B39: keep at most image_count - 1 flips outstanding, so there is
         * always one buffer the display engine is not queued to read. Reaping
         * here rather than after every flip is what gives the pacing without
         * putting a scanout interval on the frame's critical path. */
        const uint64_t flip_wait_start = vk_ps4_present_clock();
        sc->async_flip_enabled = vk_ps4_async_flip_enabled();
        if (sc->async_flip_enabled && sc->image_count > 1) {
            while (sc->pending_flip_count + 1u >= sc->image_count) {
                bool reaped = false;
                if (!vk_ps4_video_out_reap_flip(sc, true, &reaped) || !reaped) {
                    if (q->device) q->device->gnm_present_uncertain = true;
                    vk_ps4_log("QueuePresent: flip event wait FAILED pending=%u rc=%d",
                               sc->pending_flip_count,
                               sc->video_out.last_error_code);
                    if (pPresentInfo->pResults)
                        pPresentInfo->pResults[i] = VK_ERROR_SURFACE_LOST_KHR;
                    return VK_ERROR_SURFACE_LOST_KHR;
                }
            }
        }

        const uint64_t flip_submit_start = vk_ps4_present_clock();
        const uint64_t flip_wait_us = flip_submit_start - flip_wait_start;
        GnmError flip_result = (sc->async_flip_enabled && sc->image_count > 1)
            ? vk_ps4_video_out_flip_submit(
                  &sc->video_out, image_index, (int64_t)sc->video_out.frame,
                  GNM_VIDEO_OUT_FLIP_VSYNC)
            : vk_ps4_video_out_flip_bounded(
                  &sc->video_out, image_index, (int64_t)sc->video_out.frame,
                  GNM_VIDEO_OUT_FLIP_VSYNC);
        const uint64_t flip_submit_us = vk_ps4_present_clock() - flip_submit_start;
        if (flip_result != GNM_ERROR_OK) {
            if (q->device) q->device->gnm_present_uncertain = true;
            vk_ps4_log("QueuePresent: flip FAILED img=%u stage=%u rc=%d detail=%d",
                       image_index, sc->video_out.last_error_stage,
                       (int)flip_result, sc->video_out.last_error_code);
            if (pPresentInfo->pResults) {
                pPresentInfo->pResults[i] = VK_ERROR_SURFACE_LOST_KHR;
            }
            return VK_ERROR_SURFACE_LOST_KHR;
        }
        if (sc->async_flip_enabled && sc->image_count > 1) {
            /* Ownership stays latched: the display engine has been told to
             * read this buffer and has not yet reported that it started. The
             * next reap is what clears it. */
            sc->pending_flips[sc->pending_flip_count++] = image_index;
        } else {
            sc->flip_submission_uncertain = false;
            sc->last_present_confirmed = true;
        }
        /* Both flip paths own video_out.frame.  Do not increment it again. */
        if (pPresentInfo->pResults) pPresentInfo->pResults[i] = VK_SUCCESS;
        if (trace_success) vk_ps4_log_raw("QueuePresent: flip done");

        /* DIAGNOSTIC (black-screen investigation): the GPU is done writing
         * this buffer - the render completion was proved above - so the
         * display-linear memory is safe to read from the CPU.  Dump a few
         * pixels from the corners and center to prove (or disprove) that
         * real, non-zero color data reached the presented buffer.
         * Rate-limited like the flip trace above. */
        if (trace_success && vk_ps4_pixel_probes_enabled() && image_index < sc->image_count) {
            void *base = sc->image_memory[image_index].gnm_mem.mapped;
            if (base) {
                const uint32_t pitch = sc->video_out.pitch ? sc->video_out.pitch
                                                            : sc->video_out.width * 4u;
                const uint32_t w = sc->video_out.width;
                const uint32_t h = sc->video_out.height;
                const uint8_t *p0 = (const uint8_t *)base;
                const uint8_t *pc = (const uint8_t *)base + (h / 2) * pitch + (w / 2) * 4u;
                const uint8_t *pl = (const uint8_t *)base + (h - 1) * pitch;
                vk_ps4_log("QueuePresent: pixel dump img=%u pitch=%u "
                           "top-left=%02x%02x%02x%02x center=%02x%02x%02x%02x "
                           "bottom-left=%02x%02x%02x%02x",
                           image_index, pitch,
                           p0[0], p0[1], p0[2], p0[3],
                           pc[0], pc[1], pc[2], pc[3],
                           pl[0], pl[1], pl[2], pl[3]);
            } else {
                vk_ps4_log("QueuePresent: pixel dump img=%u SKIPPED (no mapped memory)",
                           image_index);
            }
        }
        const uint64_t present_us = vk_ps4_present_clock() - present_start;
        sc->perf_present_samples++;
        sc->perf_render_wait_us += render_wait_us;
        sc->perf_flip_wait_us += flip_wait_us;
        sc->perf_flip_submit_us += flip_submit_us;
        sc->perf_present_us += present_us;
        if (present_us > sc->perf_present_max_us) sc->perf_present_max_us = present_us;
        if (sc->perf_present_samples == 120u) {
            vk_ps4_log("PRESENT_PERF: samples=120 renderWaitMeanUs=%llu flipWaitMeanUs=%llu flipSubmitMeanUs=%llu totalMeanUs=%llu totalMaxUs=%llu pixelReadback=%u",
                (unsigned long long)(sc->perf_render_wait_us / 120u),
                (unsigned long long)(sc->perf_flip_wait_us / 120u),
                (unsigned long long)(sc->perf_flip_submit_us / 120u),
                (unsigned long long)(sc->perf_present_us / 120u),
                (unsigned long long)sc->perf_present_max_us,
                (unsigned)vk_ps4_pixel_probes_enabled());
            sc->perf_present_samples = sc->perf_render_wait_us = sc->perf_flip_wait_us = 0;
            sc->perf_flip_submit_us = sc->perf_present_us = sc->perf_present_max_us = 0;
        }
        if (!(sc->async_flip_enabled && sc->image_count > 1)) {
            /* Blocking path: the flip is complete, so the display engine is
             * done with the buffer it replaced and with this one's handover.
             * Clear the in-flight tracking so AcquireNextImageKHR can reclaim
             * this image. The asynchronous path clears it from the reap
             * instead, once the event actually says so. */
            sc->image_in_flight[image_index] = false;
            sc->image_fences[image_index] = NULL;
        }
    }

    /* Signal semaphores (Vulkan 1.1+ via VkPresentTimesInfoGOOGLE etc.
     * Vulkan 1.0 VkPresentInfoKHR has no signal semaphores) */
    if (q->device) {
        /* With flips outstanding the display engine still owns a buffer, and
         * DestroyDevice fails closed on that latch by design. */
        bool any_pending = false;
        for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
            const VkPs4Swapchain *sc =
                (const VkPs4Swapchain *)pPresentInfo->pSwapchains[i];
            if (sc && sc->pending_flip_count > 0) any_pending = true;
        }
        q->device->gnm_present_in_progress = any_pending;
    }
    (void)q;
    return VK_SUCCESS;
}
