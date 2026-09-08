/*
 * vk_ps4_image.c — VkImage / VkImageView implementation.
 *
 * VkImage with color attachment usage → GnmRenderTarget
 * VkImage with depth/stencil usage → GnmDepthRenderTarget
 * VkImage with sampled/storage usage → GnmTexture
 * VkImageView → copy/modify the GnmTexture or GnmRenderTarget descriptor
 */

#include "vk_ps4_internal.h"
#include "vk_ps4_texture_address.h"
#include "vk_ps4_subresource.h"

#include <string.h>

static GnmTextureType vk_image_type_to_gnm(VkImageType type) {
    switch (type) {
    case VK_IMAGE_TYPE_1D: return GNM_TEXTURE_1D;
    case VK_IMAGE_TYPE_2D: return GNM_TEXTURE_2D;
    case VK_IMAGE_TYPE_3D: return GNM_TEXTURE_3D;
    default: return GNM_TEXTURE_2D;
    }
}

static bool vk_image_is_color_target(const VkImageCreateInfo *ci) {
    return (ci->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
}

static bool vk_image_is_depth(const VkImageCreateInfo *ci) {
    return (ci->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
}

static bool vk_image_is_linear_rgba8(const VkImageCreateInfo *ci) {
    if (!ci || ci->tiling != VK_IMAGE_TILING_LINEAR ||
        ci->imageType != VK_IMAGE_TYPE_2D || ci->mipLevels != 1 ||
        ci->arrayLayers != 1 || ci->extent.depth != 1)
        return false;
    switch (ci->format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
    default:
        return false;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
    if (!device || !pCreateInfo || !pImage ||
        pCreateInfo->extent.width == 0u ||
        pCreateInfo->extent.height == 0u ||
        pCreateInfo->extent.depth == 0u ||
        pCreateInfo->arrayLayers == 0u ||
        pCreateInfo->arrayLayers > 8192u) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pCreateInfo->mipLevels == 0 || pCreateInfo->mipLevels > 15 ||
        pCreateInfo->extent.width > 16384 || pCreateInfo->extent.height > 16384)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if ((vk_image_is_color_target(pCreateInfo) || vk_image_is_depth(pCreateInfo)) &&
        (pCreateInfo->mipLevels != 1 || pCreateInfo->tiling != VK_IMAGE_TILING_OPTIMAL || pCreateInfo->imageType != VK_IMAGE_TYPE_2D ||
         pCreateInfo->extent.depth != 1 || pCreateInfo->arrayLayers > 2048)) {
        vk_ps4_log("B2 CreateImage: attachment mip/volume/range not supported");
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4Image *img = vk_ps4_alloc_zero(alloc, sizeof(*img), 16);
    if (!img) return VK_ERROR_OUT_OF_HOST_MEMORY;
    img->type = VK_PS4_OBJ_IMAGE;
    img->device = dev;
    img->create_info = *pCreateInfo;
    img->memory = NULL;
    img->memory_offset = 0;
    img->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    img->is_render_target = false;
    img->is_depth_target = false;

    GnmDataFormat gnm_fmt = vk_ps4_vk_format_to_gnm(pCreateInfo->format);
    if (gnm_fmt.asuint == GNM_FMT_INVALID.asuint) {
        vk_ps4_free(alloc, img);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    GnmGpuMode gpu_mode = GNM_GPU_BASE;

    /* Validate mipLevels — 0 means auto, clamp to 1 */
    uint32_t mip_levels = pCreateInfo->mipLevels;
    if (mip_levels == 0) mip_levels = 1;

    /* Validate samples — PS4 supports up to 8x MSAA.
     * VkSampleCountFlagBits is a bitfield (1,2,4,8,16). */
    uint32_t samples = pCreateInfo->samples;
    if (samples == 0) samples = 1;
    if (samples != 1) {
        /* MSAA allocation, resolve and synchronization are not yet complete in
         * this ICD. Reject it rather than silently changing the sample count. */
        vk_ps4_free(alloc, img);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (vk_image_is_color_target(pCreateInfo)) {
        /* Create as render target */
        img->is_render_target = true;
        GnmRenderTargetCreateInfo rt_ci;
        sceGnmRtInitColorTargetCreateInfo(
            &rt_ci, gnm_fmt,
            pCreateInfo->extent.width, pCreateInfo->extent.height,
            pCreateInfo->arrayLayers,  /* numslices */
            samples,                   /* numsamples */
            samples,                   /* numfragments */
            GNM_TM_DISPLAY_1D_THIN,    /* bounded micro-tiles; no macro-tile aliasing */
            gpu_mode
        );

        GnmError err = sceGnmCreateRenderTarget(&img->gnm_rt, &rt_ci);
        if (err != GNM_ERROR_OK) {
            vk_ps4_free(alloc, img);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
    } else if (vk_image_is_depth(pCreateInfo)) {
        /* Depth/stencil target — create a GnmDepthRenderTarget. */
        img->is_depth_target = true;
        img->is_render_target = false;
        memset(&img->gnm_rt, 0, sizeof(img->gnm_rt));

        /* Map VkFormat to GnmZFormat and GnmStencilFormat */
        GnmZFormat zfmt = GNM_Z_INVALID;
        GnmStencilFormat sfmt = GNM_STENCIL_INVALID;
        switch (pCreateInfo->format) {
        case VK_FORMAT_D16_UNORM:
            zfmt = GNM_Z_16; sfmt = GNM_STENCIL_INVALID; break;
        case VK_FORMAT_D32_SFLOAT:
            zfmt = GNM_Z_32_FLOAT; sfmt = GNM_STENCIL_INVALID; break;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            zfmt = GNM_Z_24; sfmt = GNM_STENCIL_8; break;
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            zfmt = GNM_Z_32_FLOAT; sfmt = GNM_STENCIL_8; break;
        default:
            zfmt = GNM_Z_32_FLOAT; sfmt = GNM_STENCIL_INVALID; break;
        }

        GnmDepthRenderTargetCreateInfo drt_ci;
        memset(&drt_ci, 0, sizeof(drt_ci));
        drt_ci.width = pCreateInfo->extent.width;
        drt_ci.height = pCreateInfo->extent.height;
        drt_ci.pitch = pCreateInfo->extent.width;
        drt_ci.numslices = pCreateInfo->arrayLayers;
        drt_ci.zfmt = zfmt;
        drt_ci.stencilfmt = sfmt;
        drt_ci.tilemodehint = GNM_TM_DEPTH_2D_THIN_128;
        drt_ci.mingpumode = gpu_mode;
        drt_ci.numfragments = 1;
        GnmError err = sceGnmCreateDepthRenderTarget(&img->gnm_drt, &drt_ci);
        if (err != GNM_ERROR_OK) {
            vk_ps4_free(alloc, img);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }

        /* Also initialize a minimal texture descriptor for size calculation
         * when the DRT creation fails or for sampled depth textures. */
        GnmTextureCreateInfo tex_ci;
        memset(&tex_ci, 0, sizeof(tex_ci));
        tex_ci.format = gnm_fmt;
        tex_ci.texturetype = vk_image_type_to_gnm(pCreateInfo->imageType);
        if (pCreateInfo->arrayLayers > 1 && pCreateInfo->imageType != VK_IMAGE_TYPE_3D)
            tex_ci.texturetype = pCreateInfo->imageType == VK_IMAGE_TYPE_1D
                ? GNM_TEXTURE_1D_ARRAY : GNM_TEXTURE_2D_ARRAY;
        tex_ci.width = pCreateInfo->extent.width;
        tex_ci.height = pCreateInfo->extent.height;
        tex_ci.depth = pCreateInfo->extent.depth;
        tex_ci.pitch = pCreateInfo->extent.width;
        tex_ci.nummiplevels = mip_levels;
        tex_ci.numslices = pCreateInfo->arrayLayers;
        tex_ci.numfragments = 1;
        tex_ci.tilemodehint = GNM_TM_DEPTH_2D_THIN_128;
        tex_ci.mingpumode = gpu_mode;
        if (sceGnmCreateTexture(&img->gnm_texture, &tex_ci) != GNM_ERROR_OK ||
            !vk_ps4_texture_normalize_array_view(
                &img->gnm_texture, pCreateInfo->arrayLayers))
            memset(&img->gnm_texture, 0, sizeof(img->gnm_texture));
    } else {
        /* Create as texture */
        if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR &&
            !vk_image_is_linear_rgba8(pCreateInfo)) {
            /* Hardware-safe linear sampled-image subset: tightly defined
             * mip-0 RGBA8/BGRA8 2D images. */
            vk_ps4_free(alloc, img);
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }
        GnmTextureCreateInfo tex_ci;
        memset(&tex_ci, 0, sizeof(tex_ci));
        tex_ci.format = gnm_fmt;
        tex_ci.texturetype = vk_image_type_to_gnm(pCreateInfo->imageType);
        if (pCreateInfo->arrayLayers > 1 && pCreateInfo->imageType != VK_IMAGE_TYPE_3D)
            tex_ci.texturetype = pCreateInfo->imageType == VK_IMAGE_TYPE_1D
                ? GNM_TEXTURE_1D_ARRAY : GNM_TEXTURE_2D_ARRAY;
        tex_ci.width = pCreateInfo->extent.width;
        tex_ci.height = pCreateInfo->extent.height;
        tex_ci.depth = pCreateInfo->extent.depth;
        tex_ci.pitch = pCreateInfo->extent.width;
        tex_ci.nummiplevels = mip_levels;
        tex_ci.numslices = pCreateInfo->arrayLayers;
        tex_ci.numfragments = 1;
        tex_ci.tilemodehint = pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR
            ? GNM_TM_DISPLAY_LINEAR_ALIGNED : GNM_TM_DISPLAY_1D_THIN;
        tex_ci.mingpumode = gpu_mode;

        GnmError err = sceGnmCreateTexture(&img->gnm_texture, &tex_ci);
        if (err != GNM_ERROR_OK) {
            vk_ps4_free(alloc, img);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        /* OpenGNM's pinned CreateTexture writes lastarray=numslices even
         * though SQ_IMG_RSRC_WORD5 stores an inclusive last slice.  A plain
         * 2D texture therefore described slices [0,1] while DEPTH remained
         * one.  Retail GCN treats that inconsistent T# as invalid and texture
         * fetches resolve to opaque black.  Vulkan's view covers exactly the
         * requested arrayLayers, so normalize the inclusive descriptor here. */
        if (!vk_ps4_texture_normalize_array_view(
                &img->gnm_texture, pCreateInfo->arrayLayers)) {
            vk_ps4_free(alloc, img);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR) {
            vk_ps4_log("TextureImage CREATE LINEAR size=%ux%u pitch=%u tile=%u format=0x%x slices=%u..%u",
                       (unsigned)pCreateInfo->extent.width,
                       (unsigned)pCreateInfo->extent.height,
                       (unsigned)sceGnmTexGetPitch(&img->gnm_texture),
                       (unsigned)img->gnm_texture.tilingindex,
                       (unsigned)gnm_fmt.asuint,
                       (unsigned)img->gnm_texture.basearray,
                       (unsigned)img->gnm_texture.lastarray);
        }
    }

    *pImage = (VkImage)img;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator) {
    if (!device || !image) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Image *img = (VkPs4Image *)image;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, img);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements) {
    (void)device;
    if (!image || !pMemoryRequirements) return;
    VkPs4Image *img = (VkPs4Image *)image;

    if (img->is_render_target) {
        uint64_t size = 0;
        uint32_t align = 0;
        sceGnmRtCalcByteSize(&size, &align, &img->gnm_rt);
        if (size == 0) {
            /* Fallback for depth targets or unconfigured RTs */
            GnmDataFormat fmt = vk_ps4_vk_format_to_gnm(img->create_info.format);
            uint32_t bpp = 4; /* default 4 bytes per pixel */
            (void)fmt;
            size = (uint64_t)img->create_info.extent.width *
                   img->create_info.extent.height * bpp;
            align = 64;
        }
        pMemoryRequirements->size = size;
        pMemoryRequirements->alignment = align ? align : 64;
    } else if (img->is_depth_target) {
        uint64_t size = 0;
        uint32_t align = 0;
        if (sceGnmDrtCalcByteSize(&size, &align, &img->gnm_drt) != GNM_ERROR_OK ||
            size == 0) {
            /* No guessed width*height fallback: tiled Z/stencil surfaces can
             * be larger due to padded pitch, slice alignment and a separate
             * stencil plane.  Make allocation fail closed rather than bind
             * an undersized surface that the DB can write past. */
            size = UINT64_MAX;
            align = 64u * 1024u;
        }
        pMemoryRequirements->size = size;
        pMemoryRequirements->alignment = align ? align : 64u * 1024u;
    } else {
        uint64_t size = 0;
        uint32_t align = 0;
        sceGnmTexCalcByteSize(&size, &align, &img->gnm_texture);
        if (size == 0) {
            uint32_t bpp = 4;
            size = (uint64_t)img->create_info.extent.width *
                   img->create_info.extent.height * bpp;
            align = 64;
        }
        pMemoryRequirements->size = size;
        pMemoryRequirements->alignment = align ? align : 64;
    }
    /* Images prefer Garlic (GPU-local) memory */
    pMemoryRequirements->memoryTypeBits = (1u << VK_PS4_MEMORY_TYPE_GARLIC) |
                                          (1u << VK_PS4_MEMORY_TYPE_ONION);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    if (!device || !image || !memory) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Image *img = (VkPs4Image *)image;
    VkPs4DeviceMemory *mem = (VkPs4DeviceMemory *)memory;

    if (!mem->gnm_mem.mapped) {
        /* Cannot bind without a mapping — return error */
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    void *gpu_addr = (char *)mem->gnm_mem.mapped + offset;

    VkMemoryRequirements req;
    memset(&req, 0, sizeof(req));
    vk_ps4_GetImageMemoryRequirements(device, image, &req);
    if (offset > mem->size || req.size > mem->size - offset ||
        (req.alignment && (offset % req.alignment) != 0))
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    if (img->is_render_target) {
        sceGnmRtSetBaseAddr(&img->gnm_rt, gpu_addr);
    } else if (img->is_depth_target) {
        if (sceGnmDrtSetZReadAddress(&img->gnm_drt, gpu_addr) != GNM_ERROR_OK ||
            sceGnmDrtSetZWriteAddress(&img->gnm_drt, gpu_addr) != GNM_ERROR_OK)
            return VK_ERROR_INITIALIZATION_FAILED;

        if (img->gnm_drt.stencilinfo.format != GNM_STENCIL_INVALID) {
            uint64_t stencil_offset = 0;
            if (sceGnmDrtCalcStencilByteOffset(
                    &stencil_offset, &img->gnm_drt) != GNM_ERROR_OK)
                return VK_ERROR_INITIALIZATION_FAILED;
            void *stencil_addr = (char *)gpu_addr + stencil_offset;
            if (sceGnmDrtSetStencilReadAddress(
                    &img->gnm_drt, stencil_addr) != GNM_ERROR_OK ||
                sceGnmDrtSetStencilWriteAddress(
                    &img->gnm_drt, stencil_addr) != GNM_ERROR_OK)
                return VK_ERROR_INITIALIZATION_FAILED;
        }
        /* Keep the sampled-depth descriptor coherent when the format mapping
         * supports one; attachment-only users ignore it. */
        if (img->gnm_texture.dataformat != 0) {
            if (!vk_ps4_texture_address_is_encodable(gpu_addr))
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            sceGnmTexSetBaseAddress(&img->gnm_texture, gpu_addr);
            if (!vk_ps4_texture_finalize_base_address(
                    &img->gnm_texture, gpu_addr))
                return VK_ERROR_INITIALIZATION_FAILED;
        }
    } else {
        if (!vk_ps4_texture_address_is_encodable(gpu_addr))
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        sceGnmTexSetBaseAddress(&img->gnm_texture, gpu_addr);
        if (!vk_ps4_texture_finalize_base_address(
                &img->gnm_texture, gpu_addr))
            return VK_ERROR_INITIALIZATION_FAILED;
        /* Binding success is not an error. Sample this high-volume path while
         * preserving every failed address/format diagnostic. Atomic because
         * texture creation can come from more than one host thread. */
        static unsigned bind_log_count;
        const unsigned bind_sample = __atomic_add_fetch(&bind_log_count, 1u, __ATOMIC_RELAXED);
        if (bind_sample <= 8u || (bind_sample % 1024u) == 0u)
        vk_ps4_log("TextureImage BIND READY base=%p encoded=%p high=%u pitch=%u tile=%u mtype=%u/%u/%u",
                   gpu_addr,
                   (void *)vk_ps4_texture_decoded_base_address(
                       &img->gnm_texture),
                   (unsigned)img->gnm_texture.baseaddresshi,
                   (unsigned)sceGnmTexGetPitch(&img->gnm_texture),
                   (unsigned)img->gnm_texture.tilingindex,
                   (unsigned)img->gnm_texture.mtype_l2,
                   (unsigned)img->gnm_texture.mtype0,
                   (unsigned)img->gnm_texture.mtype2);
    }

    img->memory = mem;
    img->memory_offset = offset;
    vk_ps4_cpu_store_fence();

    return VK_SUCCESS;
}

static GnmChannel vk_ps4_view_swizzle(VkComponentSwizzle swizzle, uint32_t identity,
                                      const GnmChannel channels[4]) {
    switch (swizzle) {
    case VK_COMPONENT_SWIZZLE_ZERO: return GNM_CHAN_CONSTANT0;
    case VK_COMPONENT_SWIZZLE_ONE: return GNM_CHAN_CONSTANT1;
    case VK_COMPONENT_SWIZZLE_R: return channels[0];
    case VK_COMPONENT_SWIZZLE_G: return channels[1];
    case VK_COMPONENT_SWIZZLE_B: return channels[2];
    case VK_COMPONENT_SWIZZLE_A: return channels[3];
    default: return channels[identity];
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator, VkImageView *pImageView) {
    if (!device || !pCreateInfo || !pImageView) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4ImageView *view = vk_ps4_alloc_zero(alloc, sizeof(*view), 16);
    if (!view) return VK_ERROR_OUT_OF_HOST_MEMORY;
    view->type = VK_PS4_OBJ_IMAGE_VIEW;
    view->device = dev;
    view->image = (VkPs4Image *)pCreateInfo->image;
    if (!view->image) {
        vk_ps4_free(alloc, view);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    view->create_info = *pCreateInfo;

    const VkImageCreateInfo *ci = &view->image->create_info;
    const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;
    uint32_t mip_count, layer_count;
    if (!view->image->memory ||
        !vk_ps4_resolve_range(range->baseMipLevel, range->levelCount, ci->mipLevels, &mip_count) ||
        !vk_ps4_resolve_range(range->baseArrayLayer, range->layerCount, ci->arrayLayers, &layer_count) ||
        pCreateInfo->format != ci->format) {
        vk_ps4_free(alloc, view);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    view->create_info.subresourceRange.levelCount = mip_count;
    view->create_info.subresourceRange.layerCount = layer_count;
    view->gnm_rt_view = view->image->gnm_rt;
    view->gnm_drt_view = view->image->gnm_drt;
    view->gnm_rt_view.view.slicestart = range->baseArrayLayer;
    view->gnm_rt_view.view.slicemax = range->baseArrayLayer + layer_count - 1;
    view->gnm_drt_view.depthview.slicestart = range->baseArrayLayer;
    view->gnm_drt_view.depthview.slicemax = range->baseArrayLayer + layer_count - 1;

    if (!view->image->is_render_target) {
        view->gnm_view = view->image->gnm_texture;
    } else {
        const GnmRenderTarget *rt = &view->image->gnm_rt;
        GnmTextureCreateInfo ti = {0};
        ti.format = sceGnmRtGetFormat(rt);
        ti.texturetype = ci->arrayLayers > 1 ? GNM_TEXTURE_2D_ARRAY : GNM_TEXTURE_2D;
        ti.width = ci->extent.width;
        ti.height = ci->extent.height;
        ti.depth = 1;
        ti.pitch = sceGnmRtGetPitch(rt);
        ti.nummiplevels = 1;
        ti.numslices = ci->arrayLayers;
        ti.numfragments = 1;
        ti.tilemodehint = (GnmTileMode)rt->attrib.tilemode_index;
        ti.mingpumode = GNM_GPU_BASE;
        void *base = sceGnmRtGetBaseAddr(rt);
        if (sceGnmCreateTexture(&view->gnm_view, &ti) != GNM_ERROR_OK) {
            vk_ps4_free(alloc, view);
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }
        sceGnmTexSetBaseAddress(&view->gnm_view, base);
        if (!vk_ps4_texture_finalize_base_address(&view->gnm_view, base)) {
            vk_ps4_free(alloc, view);
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    view->gnm_view.baselevel = range->baseMipLevel;
    view->gnm_view.lastlevel = range->baseMipLevel + mip_count - 1;
    view->gnm_view.basearray = range->baseArrayLayer;
    view->gnm_view.lastarray = range->baseArrayLayer + layer_count - 1;
    switch (pCreateInfo->viewType) {
    case VK_IMAGE_VIEW_TYPE_1D: view->gnm_view.type = GNM_TEXTURE_1D; break;
    case VK_IMAGE_VIEW_TYPE_1D_ARRAY: view->gnm_view.type = GNM_TEXTURE_1D_ARRAY; break;
    case VK_IMAGE_VIEW_TYPE_2D: view->gnm_view.type = GNM_TEXTURE_2D; break;
    case VK_IMAGE_VIEW_TYPE_2D_ARRAY: view->gnm_view.type = GNM_TEXTURE_2D_ARRAY; break;
    case VK_IMAGE_VIEW_TYPE_3D: view->gnm_view.type = GNM_TEXTURE_3D; break;
    default:
        /* Cube addressing has a distinct face/slice convention. */
        vk_ps4_free(alloc, view);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    const GnmChannel channels[4] = {view->gnm_view.dstselx, view->gnm_view.dstsely,
                                   view->gnm_view.dstselz, view->gnm_view.dstselw};
    view->gnm_view.dstselx = vk_ps4_view_swizzle(pCreateInfo->components.r, 0, channels);
    view->gnm_view.dstsely = vk_ps4_view_swizzle(pCreateInfo->components.g, 1, channels);
    view->gnm_view.dstselz = vk_ps4_view_swizzle(pCreateInfo->components.b, 2, channels);
    view->gnm_view.dstselw = vk_ps4_view_swizzle(pCreateInfo->components.a, 3, channels);

    *pImageView = (VkImageView)view;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator) {
    if (!device || !imageView) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4ImageView *view = (VkPs4ImageView *)imageView;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, view);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetImageSubresourceLayout(VkDevice device, VkImage image,
                                 const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout) {
    (void)device;
    (void)pSubresource;
    if (!image || !pLayout) return;
    VkPs4Image *img = (VkPs4Image *)image;
    memset(pLayout, 0, sizeof(*pLayout));
    if (!vk_image_is_linear_rgba8(&img->create_info)) return;
    uint64_t size = 0;
    uint32_t align = 0;
    sceGnmTexCalcByteSize(&size, &align, &img->gnm_texture);
    pLayout->offset = 0;
    pLayout->rowPitch = (VkDeviceSize)sceGnmTexGetPitch(&img->gnm_texture) * 4u;
    pLayout->arrayPitch = pLayout->rowPitch * img->create_info.extent.height;
    pLayout->depthPitch = pLayout->arrayPitch;
    pLayout->size = size;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkPs4ImageHasLinearStorage(VkImage image) {
    const VkPs4Image *img = (const VkPs4Image *)image;
    if (!img || img->is_depth_target) return VK_FALSE;
    if (img->is_swapchain_image) return VK_TRUE;
    const GnmTileMode tile = img->is_render_target
        ? (GnmTileMode)img->gnm_rt.attrib.tilemode_index
        : (GnmTileMode)img->gnm_texture.tilingindex;
    return tile == GNM_TM_DISPLAY_LINEAR_GENERAL || tile == GNM_TM_DISPLAY_LINEAR_ALIGNED;
}
