/*
 * vk_ps4_descriptor.c — Vulkan descriptor sets over GNM user-data registers.
 *
 * Vulkan descriptor sets map to GNM user-data register slots:
 *   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER   → GnmBuffer (Vsharp, 4 regs)
 *   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER   → GnmBuffer (Vsharp, 4 regs)
 *   VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE    → GnmTexture (Tsharp, 8 regs)
 *   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE    → GnmTexture (Tsharp, 8 regs)
 *   VK_DESCRIPTOR_TYPE_SAMPLER          → GnmSampler (Ssharp, 4 regs)
 *   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER → Tsharp + Ssharp
 *   VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER   → GnmBuffer (Vsharp)
 *   VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER   → GnmBuffer (Vsharp)
 *
 * The shader binary's GnmInputUsageSlot table (embedded by psbc) maps
 * Vulkan binding numbers (apislot) to GNM user-data registers
 * (startregister). At CmdBindDescriptorSets, we iterate the pipeline's
 * slot tables and emit Set*UserData PM4 commands.
 */

#include "vk_ps4_internal.h"
#include <string.h>
#include <stdio.h>

/* === Descriptor set layout === */
/* (Already implemented in vk_ps4_pipeline.c) */

/* === Descriptor pool === */
/* (Already implemented in vk_ps4_pipeline.c) */

/* === Descriptor set allocation === */
/* (Already implemented in vk_ps4_pipeline.c — but we need to initialize
 * the binding storage here. See vk_ps4_AllocateDescriptorSets below. */

/* === Helper: get GnmShaderStage from Vulkan bind point === */
static GnmShaderStage vk_bind_point_to_gnm_stage(VkPipelineBindPoint bp) {
    switch (bp) {
    case VK_PIPELINE_BIND_POINT_GRAPHICS: return GNM_STAGE_VS;
    case VK_PIPELINE_BIND_POINT_COMPUTE:  return GNM_STAGE_CS;
    default: return GNM_STAGE_VS;
    }
}

/* === Helper: find a binding in a descriptor set by Vulkan binding number === */
static VkPs4DescriptorBinding *find_binding(VkPs4DescriptorSet *set, uint32_t binding) {
    if (!set) return NULL;
    for (uint32_t i = 0; i < set->binding_count; i++) {
        if (set->bindings[i].binding_number == binding)
            return &set->bindings[i];
    }
    return NULL;
}

/* PSBC records Vulkan binding numbers, but not descriptor-set numbers.  A
 * pipeline layout therefore guarantees that a binding number is unique for
 * every shader stage.  Use the layout's stage mask while resolving a slot so
 * bindings with the same number in disjoint stages remain unambiguous. */
static VkPs4DescriptorBinding *find_stage_binding(
    VkPs4DescriptorSet *set, uint32_t binding, VkShaderStageFlagBits stage
) {
    if (!set || !set->layout) return NULL;
    for (uint32_t i = 0; i < set->binding_count; i++) {
        if (set->bindings[i].binding_number == binding &&
            i < set->layout->binding_count &&
            (set->layout->bindings[i].stageFlags & stage) != 0)
            return &set->bindings[i];
    }
    return NULL;
}

/* Vulkan consumes dynamic offsets in set order, then binding-number order,
 * then array-element order.  Compute the index directly instead of storing a
 * fixed-size temporary map. */
static bool find_dynamic_offset(
    uint32_t first_set, uint32_t set_count,
    const VkDescriptorSet *sets, uint32_t target_set,
    uint32_t target_binding, uint32_t target_element,
    uint32_t dynamic_offset_count, const uint32_t *dynamic_offsets,
    uint32_t *out_offset
) {
    if (!sets || !dynamic_offsets || !out_offset ||
        target_set < first_set || target_set - first_set >= set_count)
        return false;

    uint64_t offset_index = 0;
    uint32_t target_count = 0;
    bool target_found = false;
    for (uint32_t si = 0; si < set_count; si++) {
        VkPs4DescriptorSet *set = (VkPs4DescriptorSet *)sets[si];
        if (!set) continue;
        const uint32_t set_number = first_set + si;
        if (set_number > target_set) break;
        for (uint32_t bi = 0; bi < set->binding_count; bi++) {
            VkPs4DescriptorBinding *next = &set->bindings[bi];
            const bool dynamic =
                next->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                next->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            if (!dynamic) continue;
            if (set_number < target_set ||
                (set_number == target_set &&
                 next->binding_number < target_binding)) {
                offset_index += next->count;
                continue;
            }
            if (set_number == target_set &&
                next->binding_number == target_binding) {
                target_count = next->count;
                target_found = true;
            }
        }
    }
    offset_index += target_element;
    if (!target_found || target_element >= target_count ||
        offset_index >= dynamic_offset_count)
        return false;
    *out_offset = dynamic_offsets[offset_index];
    return true;
}

/* Select a pointer table from the PSBC usage kind, not from whichever array
 * happens to be non-NULL first.  A combined image/sampler binding owns both a
 * GnmTexture table and a GnmSampler table.  In that case PTR_SAMPLERTABLE must
 * point at samplers while PTR_RESOURCETABLE must point at textures.  Choosing
 * buffers -> textures -> samplers silently fed a Tsharp table to an Ssharp
 * dereference on retail hardware. */
static void *descriptor_table_for_usage(
    const VkPs4DescriptorBinding *binding, uint8_t usage_type
) {
    if (!binding) return NULL;

    switch ((GnmShaderInputUsageType)usage_type) {
    case GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE:
        switch (binding->type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            return binding->buffers;
        default:
            return NULL;
        }
    case GNM_SHINPUTUSAGE_PTR_SAMPLERTABLE:
        switch (binding->type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return binding->samplers;
        default:
            return NULL;
        }
    case GNM_SHINPUTUSAGE_PTR_RESOURCETABLE:
        switch (binding->type) {
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            return binding->textures;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            return binding->buffers;
        default:
            return NULL;
        }
    case GNM_SHINPUTUSAGE_PTR_RWRESOURCETABLE:
        switch (binding->type) {
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return binding->textures;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            return binding->buffers;
        default:
            return NULL;
        }
    default:
        return NULL;
    }
}

static bool descriptor_table_is_direct(
    const VkPs4DescriptorBinding *binding, const void *table
) {
    if (!binding || !table || !binding->resource_mem.allocated ||
        !binding->resource_mem.mapped || binding->resource_mem.size == 0)
        return false;

    const uintptr_t base = (uintptr_t)binding->resource_mem.mapped;
    const uintptr_t address = (uintptr_t)table;
    return address >= base && address - base < binding->resource_mem.size;
}

static uint32_t descriptor_stride(VkDescriptorType type) {
    switch (type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER: return sizeof(GnmSampler);
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return sizeof(GnmTexture) + sizeof(GnmSampler);
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        return sizeof(GnmTexture);
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        return sizeof(GnmBuffer);
    default: return 0;
    }
}

static void *descriptor_element(const VkPs4DescriptorBinding *b,
                                uint32_t element, uint32_t component_offset) {
    if (!b || !b->table_base || element >= b->count) return NULL;
    return b->table_base + (uint64_t)element * b->descriptor_stride +
           component_offset;
}

static GnmBuffer *buffer_element(const VkPs4DescriptorBinding *b,
                                 uint32_t element) {
    return (GnmBuffer *)descriptor_element(b, element, 0);
}

static GnmTexture *texture_element(const VkPs4DescriptorBinding *b,
                                   uint32_t element) {
    return (GnmTexture *)descriptor_element(b, element, 0);
}

static GnmSampler *sampler_element(const VkPs4DescriptorBinding *b,
                                   uint32_t element) {
    const uint32_t offset =
        b && b->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            ? sizeof(GnmTexture) : 0;
    return (GnmSampler *)descriptor_element(b, element, offset);
}

VkResult vk_ps4_allocate_descriptor_set_resources(
    VkPs4DescriptorSet *set, const VkAllocationCallbacks *alloc
) {
    (void)alloc;
    if (!set || !set->layout) return VK_ERROR_INITIALIZATION_FAILED;

    uint64_t table_bytes = 0;
    for (uint32_t i = 0; i < set->binding_count; ++i) {
        VkPs4DescriptorBinding *b = &set->bindings[i];
        const uint64_t end = (uint64_t)b->table_offset +
            (uint64_t)b->descriptor_stride * b->count;
        if (end > table_bytes) table_bytes = end;
    }
    table_bytes = (table_bytes + 15u) & ~15ull;
    if (table_bytes == 0) {
        for (uint32_t i = 0; i < set->binding_count; ++i)
            set->bindings[i].resources_allocated = true;
        return VK_SUCCESS;
    }

    memset(&set->descriptor_mem, 0, sizeof(set->descriptor_mem));
    GnmError err = sceGnmDirectMemoryAllocate(
        &set->descriptor_mem, table_bytes, 64u * 1024u,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW);
    if (err != GNM_ERROR_OK || !set->descriptor_mem.mapped) {
        memset(&set->descriptor_mem, 0, sizeof(set->descriptor_mem));
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    if ((uint32_t)((uint64_t)(uintptr_t)set->descriptor_mem.mapped >> 32u) !=
        VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI) {
        vk_ps4_log("descriptor: SET_ADDRESS REJECT table=%p high=%u expected=%u",
                   set->descriptor_mem.mapped,
                   (uint32_t)((uint64_t)(uintptr_t)set->descriptor_mem.mapped >> 32u),
                   VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI);
        sceGnmDirectMemoryRelease(&set->descriptor_mem);
        memset(&set->descriptor_mem, 0, sizeof(set->descriptor_mem));
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    memset(set->descriptor_mem.mapped, 0, table_bytes);
    for (uint32_t i = 0; i < set->binding_count; ++i) {
        VkPs4DescriptorBinding *b = &set->bindings[i];
        b->table_base = (uint8_t *)set->descriptor_mem.mapped + b->table_offset;
        b->buffers = NULL;
        b->textures = NULL;
        b->samplers = NULL;
        switch (b->type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            b->buffers = (GnmBuffer *)b->table_base; break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            b->samplers = (GnmSampler *)b->table_base; break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            b->textures = (GnmTexture *)b->table_base; break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            b->textures = (GnmTexture *)b->table_base;
            b->samplers = (GnmSampler *)(b->table_base + sizeof(GnmTexture));
            break;
        default: break;
        }
        b->resources_allocated = true;
    }
    vk_ps4_cpu_store_fence();
    return VK_SUCCESS;
}

void vk_ps4_release_descriptor_set_resources(VkPs4DescriptorSet *set) {
    if (!set) return;
    if (set->descriptor_mem.allocated)
        sceGnmDirectMemoryRelease(&set->descriptor_mem);
    memset(&set->descriptor_mem, 0, sizeof(set->descriptor_mem));
    for (uint32_t i = 0; i < set->binding_count; ++i) {
        VkPs4DescriptorBinding *b = &set->bindings[i];
        b->table_base = NULL;
        b->buffers = NULL;
        b->textures = NULL;
        b->samplers = NULL;
        b->resources_allocated = false;
    }
}

/* === Helper: allocate resource arrays for a binding === */
VkResult vk_ps4_allocate_descriptor_binding_resources(
    VkPs4DescriptorBinding *b, const VkAllocationCallbacks *alloc
) {
    if (!b) return VK_ERROR_INITIALIZATION_FAILED;
    if (b->resources_allocated) return VK_SUCCESS;
    if (b->count == 0) {
        b->resources_allocated = true;
        return VK_SUCCESS;
    }
    (void)alloc;

    uint64_t buffer_bytes = 0;
    uint64_t texture_bytes = 0;
    uint64_t sampler_bytes = 0;
    switch (b->type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        buffer_bytes = (uint64_t)b->count * sizeof(GnmBuffer);
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        sampler_bytes = (uint64_t)b->count * sizeof(GnmSampler);
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        texture_bytes = (uint64_t)b->count * sizeof(GnmTexture);
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        texture_bytes = (uint64_t)b->count * sizeof(GnmTexture);
        sampler_bytes = (uint64_t)b->count * sizeof(GnmSampler);
        break;
    default:
        /* Unsupported descriptor type */
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    const uint64_t texture_offset = (buffer_bytes + 15u) & ~15ull;
    const uint64_t sampler_offset =
        (texture_offset + texture_bytes + 15u) & ~15ull;
    const uint64_t total_bytes = sampler_offset + sampler_bytes;
    if (total_bytes == 0) return VK_ERROR_FEATURE_NOT_PRESENT;

    /* PSBC's compact sampled-world ABI is intentionally one element only:
     * [GnmTexture:32][GnmSampler:16].  The generic allocation order is not
     * interleaved for arrays, so reject any combined array instead of letting
     * the shader read a neighbouring T# as its S#. */
    if (b->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
        (b->count != 1u || texture_offset != 0u ||
         sampler_offset != VK_PS4_PSBC_COMBINED_SAMPLER_OFFSET ||
         total_bytes != VK_PS4_PSBC_COMBINED_TABLE_BYTES)) {
        vk_ps4_log("descriptor: PSBC_COMBINED_LAYOUT REJECT binding=%u count=%u texoff=%llu sampoff=%llu bytes=%llu",
                   b->binding_number, b->count,
                   (unsigned long long)texture_offset,
                   (unsigned long long)sampler_offset,
                   (unsigned long long)total_bytes);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    memset(&b->resource_mem, 0, sizeof(b->resource_mem));
    GnmError err = sceGnmDirectMemoryAllocate(
        &b->resource_mem, total_bytes, 64u * 1024u,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
    );
    if (err != GNM_ERROR_OK || !b->resource_mem.mapped) {
        memset(&b->resource_mem, 0, sizeof(b->resource_mem));
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    if (b->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
        const uint64_t table_address =
            (uint64_t)(uintptr_t)b->resource_mem.mapped;
        const uint32_t address_hi = (uint32_t)(table_address >> 32u);
        if (address_hi != VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI) {
            vk_ps4_log("descriptor: PSBC_COMBINED_ADDRESS REJECT binding=%u table=%p high=%u expected=%u",
                       b->binding_number, b->resource_mem.mapped, address_hi,
                       VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI);
            sceGnmDirectMemoryRelease(&b->resource_mem);
            memset(&b->resource_mem, 0, sizeof(b->resource_mem));
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        vk_ps4_log("descriptor: PSBC_COMBINED_TABLE binding=%u table=%p high=%u image=0 sampler=32 bytes=48",
                   b->binding_number, b->resource_mem.mapped, address_hi);
    }

    memset(b->resource_mem.mapped, 0, total_bytes);
    char *base = (char *)b->resource_mem.mapped;
    b->buffers = buffer_bytes ? (GnmBuffer *)base : NULL;
    b->textures = texture_bytes
        ? (GnmTexture *)(base + texture_offset) : NULL;
    b->samplers = sampler_bytes
        ? (GnmSampler *)(base + sampler_offset) : NULL;
    b->resources_allocated = true;
    vk_ps4_cpu_store_fence();
    return VK_SUCCESS;
}

/* === Helper: free resource arrays for a binding === */
void vk_ps4_release_descriptor_binding_resources(
    VkPs4DescriptorBinding *b, const VkAllocationCallbacks *alloc
) {
    if (!b) return;
    if (b->table_base) {
        b->table_base = NULL;
        b->buffers = NULL;
        b->textures = NULL;
        b->samplers = NULL;
        b->resources_allocated = false;
        return;
    }
    if (b->resource_mem.allocated) {
        sceGnmDirectMemoryRelease(&b->resource_mem);
    } else {
        /* Compatibility with descriptor sets made by an older host build. */
        if (b->buffers) vk_ps4_free(alloc, b->buffers);
        if (b->textures) vk_ps4_free(alloc, b->textures);
        if (b->samplers) vk_ps4_free(alloc, b->samplers);
    }
    memset(&b->resource_mem, 0, sizeof(b->resource_mem));
    b->buffers = NULL;
    b->textures = NULL;
    b->samplers = NULL;
    b->table_base = NULL;
    b->resources_allocated = false;
}

/* === Helper: build a GnmBuffer from a VkDescriptorBufferInfo === */
static void build_buffer_descriptor(GnmBuffer *gnm_buf, VkPs4Buffer *vk_buf,
                                     VkDeviceSize offset, VkDeviceSize range,
                                     VkDescriptorType desc_type) {
    if (!vk_buf || !vk_buf->memory || !vk_buf->memory->gnm_mem.mapped) {
        memset(gnm_buf, 0, sizeof(*gnm_buf));
        return;
    }

    /* A V# authorizes GPU access independently of the Vulkan buffer handle.
     * Check explicit ranges as well as WHOLE_SIZE before constructing one;
     * truncating VkDeviceSize to 32 bits or accepting an overlong range would
     * allow an invalid descriptor write to escape the backing allocation. */
    if (offset > vk_buf->create_info.size) {
        memset(gnm_buf, 0, sizeof(*gnm_buf));
        return;
    }
    const VkDeviceSize remaining = vk_buf->create_info.size - offset;
    const VkDeviceSize requested = range == VK_WHOLE_SIZE ? remaining : range;
    if (requested == 0 || requested > remaining || requested > UINT32_MAX) {
        memset(gnm_buf, 0, sizeof(*gnm_buf));
        return;
    }
    const uint32_t size = (uint32_t)requested;
    void *gpu_addr = (char *)vk_buf->memory->gnm_mem.mapped +
                     vk_buf->memory_offset + offset;

    *gnm_buf = sceGnmCreateConstBuffer(gpu_addr, size);

    /* Storage buffers are raw buffers: stride 0, and with stride 0 the
     * hardware reads NUM_RECORDS as a byte count. The const-buffer
     * constructor above leaves size/16 in it, which bounded the character
     * renderer's bone matrices to the first 960 bytes - 15 of 240 bones -
     * and returned zeros for every other bone (B4 test 13 descriptor dump:
     * "set2 b0 V# stride=0 records=960" for a 15360-byte buffer). */
    if (desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
        desc_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
        gnm_buf->stride = 0;
        gnm_buf->numrecords = size;
    }
}

/* === Helper: build a GnmSampler from VkSamplerCreateInfo fields === */
static void build_sampler_descriptor(GnmSampler *s, const VkSamplerCreateInfo *ci) {
    memset(s, 0, sizeof(*s));

    /* Clamp modes — GnmTexClamp enum values are non-sequential. */
    static const GnmTexClamp vk_to_gnm_clamp[] = {
        [VK_SAMPLER_ADDRESS_MODE_REPEAT] = GNM_TEX_CLAMP_WRAP,
        [VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT] = GNM_TEX_CLAMP_MIRROR,
        [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE] = GNM_TEX_CLAMP_CLAMP_LAST_TEXEL,
        [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER] = GNM_TEX_CLAMP_CLAMP_BORDER,
        [VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE] = GNM_TEX_CLAMP_MIRROR_ONCE_LAST_TEXEL,
    };
    if (ci->addressModeU < sizeof(vk_to_gnm_clamp)/sizeof(vk_to_gnm_clamp[0]))
        s->clampx = vk_to_gnm_clamp[ci->addressModeU];
    if (ci->addressModeV < sizeof(vk_to_gnm_clamp)/sizeof(vk_to_gnm_clamp[0]))
        s->clampy = vk_to_gnm_clamp[ci->addressModeV];
    if (ci->addressModeW < sizeof(vk_to_gnm_clamp)/sizeof(vk_to_gnm_clamp[0]))
        s->clampz = vk_to_gnm_clamp[ci->addressModeW];

    /* Filter modes — GnmFilter: POINT=0, BILINEAR=1, ANISO_POINT=2, ANISO_BILINEAR=3.
     * When anisotropy is enabled, use ANISO_* variants. */
    s->filtermode = 0; /* 0 = sync, 1 = async */
    if (ci->anisotropyEnable) {
        s->xymagfilter = (ci->magFilter == VK_FILTER_LINEAR)
            ? GNM_FILTER_ANISO_BILINEAR : GNM_FILTER_ANISO_POINT;
        s->xyminfilter = (ci->minFilter == VK_FILTER_LINEAR)
            ? GNM_FILTER_ANISO_BILINEAR : GNM_FILTER_ANISO_POINT;
    } else {
        s->xymagfilter = (ci->magFilter == VK_FILTER_LINEAR)
            ? GNM_FILTER_BILINEAR : GNM_FILTER_POINT;
        s->xyminfilter = (ci->minFilter == VK_FILTER_LINEAR)
            ? GNM_FILTER_BILINEAR : GNM_FILTER_POINT;
    }
    /* GnmMipFilter: NONE=0, POINT=1, LINEAR=2 */
    if (ci->mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR) {
        s->mipfilter = GNM_MIPFILTER_LINEAR;
    } else {
        s->mipfilter = GNM_MIPFILTER_POINT;
    }

    /* LOD — clamp to non-negative for min/max LOD. lodbias is s4.10 fixed-point. */
    float min_lod = ci->minLod > 0.0f ? ci->minLod : 0.0f;
    float max_lod = ci->maxLod > 0.0f ? ci->maxLod : 0.0f;
    s->minlod = (uint32_t)(min_lod * 256.0f) & 0xFFF;
    s->maxlod = (uint32_t)(max_lod * 256.0f) & 0xFFF;
    /* lodbias is a signed 14-bit s4.10 fixed-point (10 fractional bits → scale 1024). */
    int32_t bias = (int32_t)(ci->mipLodBias * 1024.0f);
    s->lodbias = (uint32_t)(bias & 0x3FFF);

    /* Anisotropic filtering — GNM maxanisoratio is a 3-bit index:
     * 0=1x, 1=2x, 2=4x, 3=8x, 4=16x. */
    if (ci->anisotropyEnable) {
        float a = ci->maxAnisotropy;
        if (a >= 16.0f) s->maxanisoratio = 4;
        else if (a >= 8.0f) s->maxanisoratio = 3;
        else if (a >= 4.0f) s->maxanisoratio = 2;
        else if (a >= 2.0f) s->maxanisoratio = 1;
        else s->maxanisoratio = 0;
    }

    /* Compare */
    if (ci->compareEnable) {
        /* Map VkCompareOp to GNM depth compare function.
         * GNM uses the same encoding as VkCompareOp (0=NEVER, 1=LESS, etc.) */
        static const uint32_t vk_to_gnm_compare[] = {
            [VK_COMPARE_OP_NEVER] = 0,
            [VK_COMPARE_OP_LESS] = 1,
            [VK_COMPARE_OP_EQUAL] = 2,
            [VK_COMPARE_OP_LESS_OR_EQUAL] = 3,
            [VK_COMPARE_OP_GREATER] = 4,
            [VK_COMPARE_OP_NOT_EQUAL] = 5,
            [VK_COMPARE_OP_GREATER_OR_EQUAL] = 6,
            [VK_COMPARE_OP_ALWAYS] = 7,
        };
        if (ci->compareOp < sizeof(vk_to_gnm_compare)/sizeof(vk_to_gnm_compare[0]))
            s->depthcomparefunc = vk_to_gnm_compare[ci->compareOp];
    } else {
        s->depthcomparefunc = 7; /* ALWAYS — no comparison */
    }

    /* Border color — GnmBorderColor: TRANS_BLACK=0, OPAQUE_BLACK=1, OPAQUE_WHITE=2. */
    if (ci->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        ci->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
        ci->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) {
        switch (ci->borderColor) {
        case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
        case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
            s->bordercolortype = GNM_BORDER_COLOR_TRANS_BLACK;
            break;
        case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
        case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
            s->bordercolortype = GNM_BORDER_COLOR_OPAQUE_WHITE;
            break;
        case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
        case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
        default:
            s->bordercolortype = GNM_BORDER_COLOR_OPAQUE_BLACK;
            break;
        }
    }

    /* Force normalized coordinates (Vulkan always uses normalized) */
    s->forceunormalized = 0;
}

/* === vkUpdateDescriptorSets === */
VKAPI_ATTR void VKAPI_CALL
vk_ps4_UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                            const VkWriteDescriptorSet *pDescriptorWrites,
                            uint32_t descriptorCopyCount,
                            const VkCopyDescriptorSet *pDescriptorCopies) {
    if (!device) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;

    /* Process writes */
    for (uint32_t i = 0; i < descriptorWriteCount; i++) {
        const VkWriteDescriptorSet *w = &pDescriptorWrites[i];
        VkPs4DescriptorSet *set = (VkPs4DescriptorSet *)w->dstSet;
        if (!set) continue;

        VkPs4DescriptorBinding *b = find_binding(set, w->dstBinding);
        if (!b || b->count == 0) continue;

        /* Ensure the binding has resource arrays allocated.
         * AllocateDescriptorSets sets type/count/binding_number but doesn't
         * allocate the resource arrays — that happens lazily here.
         * Never overwrite b->count, b->binding_number, or b->type from the
         * write — those come from the layout and are immutable. */
        if (!b->resources_allocated) {
            if (vk_ps4_allocate_descriptor_binding_resources(
                    b, alloc) != VK_SUCCESS) {
                continue;
            }
        }

        /* Clamp write range to binding array bounds (avoid underflow) */
        uint32_t dst_start = w->dstArrayElement;
        uint32_t count = w->descriptorCount;
        if (dst_start >= b->count) continue;
        if (dst_start + count > b->count) {
            count = b->count - dst_start;
        }

        /* Use b->type (layout-declared type) for the switch, not w->descriptorType.
         * This prevents NULL deref if a second write uses a different type. */
        switch (b->type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            for (uint32_t j = 0; j < count; j++) {
                const VkDescriptorBufferInfo *bi = &w->pBufferInfo[j];
                VkPs4Buffer *vk_buf = (VkPs4Buffer *)bi->buffer;
                GnmBuffer *dst = buffer_element(b, dst_start + j);
                if (!dst) continue;
                if (!vk_buf) {
                    memset(dst, 0, sizeof(*dst));
                    continue;
                }
                build_buffer_descriptor(dst, vk_buf,
                                        bi->offset, bi->range, b->type);
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
            for (uint32_t j = 0; j < count; j++) {
                const VkDescriptorImageInfo *ii = &w->pImageInfo[j];
                VkPs4ImageView *view = (VkPs4ImageView *)ii->imageView;
                GnmTexture *dst = texture_element(b, dst_start + j);
                if (!dst) continue;
                if (view && view->image) {
                    /* Use the view's GnmTexture descriptor for both regular
                     * textures and render targets.  RT-as-texture is handled
                     * in CreateImageView which builds a GnmTexture from the
                     * GnmRenderTarget. */
                    *dst = view->gnm_view;
                } else {
                    memset(dst, 0, sizeof(*dst));
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLER: {
            for (uint32_t j = 0; j < count; j++) {
                const GnmSampler *s = (const GnmSampler *)w->pImageInfo[j].sampler;
                GnmSampler *dst = sampler_element(b, dst_start + j);
                if (!dst) continue;
                if (s) {
                    *dst = *s;
                } else {
                    memset(dst, 0, sizeof(*dst));
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            for (uint32_t j = 0; j < count; j++) {
                const VkDescriptorImageInfo *ii = &w->pImageInfo[j];
                VkPs4ImageView *view = (VkPs4ImageView *)ii->imageView;
                const GnmSampler *s = (const GnmSampler *)ii->sampler;
                GnmTexture *texture = texture_element(b, dst_start + j);
                GnmSampler *sampler = sampler_element(b, dst_start + j);
                if (!texture || !sampler) continue;
                if (view && view->image) {
                    *texture = view->gnm_view;
                } else {
                    memset(texture, 0, sizeof(*texture));
                }
                if (s) {
                    *sampler = *s;
                } else {
                    memset(sampler, 0, sizeof(*sampler));
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
            /* Texel buffer views: copy the GnmBuffer (V#) descriptor from
             * each VkBufferView into the binding's buffer array.  The V#
             * was built in CreateBufferView with the correct base address,
             * format, stride, and num records. */
            for (uint32_t j = 0; j < count; j++) {
                VkPs4BufferView *bv = (VkPs4BufferView *)w->pTexelBufferView[j];
                GnmBuffer *dst = buffer_element(b, dst_start + j);
                if (!dst) continue;
                if (bv) {
                    *dst = bv->gnm_buffer;
                } else {
                    memset(dst, 0, sizeof(*dst));
                }
            }
            break;
        }
        default:
            break;
        }
    }

    /* Process copies */
    for (uint32_t i = 0; i < descriptorCopyCount; i++) {
        const VkCopyDescriptorSet *c = &pDescriptorCopies[i];
        VkPs4DescriptorSet *dst = (VkPs4DescriptorSet *)c->dstSet;
        VkPs4DescriptorSet *src = (VkPs4DescriptorSet *)c->srcSet;
        if (!dst || !src) continue;

        VkPs4DescriptorBinding *db = find_binding(dst, c->dstBinding);
        VkPs4DescriptorBinding *sb = find_binding(src, c->srcBinding);
        if (!db || !sb || db->type != sb->type) continue;

        /* Ensure dst resources are allocated */
        if (!db->resources_allocated) {
            if (vk_ps4_allocate_descriptor_binding_resources(
                    db, alloc) != VK_SUCCESS)
                continue;
        }

        /* Clamp copy count (avoid underflow) */
        uint32_t count = c->descriptorCount;
        if (c->dstArrayElement >= db->count || c->srcArrayElement >= sb->count)
            continue;
        if (c->dstArrayElement + count > db->count)
            count = db->count - c->dstArrayElement;
        if (c->srcArrayElement + count > sb->count)
            count = sb->count - c->srcArrayElement;

        const uint32_t copy_stride = descriptor_stride(db->type);
        if (copy_stride == db->descriptor_stride &&
            copy_stride == sb->descriptor_stride) {
            for (uint32_t e = 0; e < count; ++e) {
                void *dst_element = descriptor_element(
                    db, c->dstArrayElement + e, 0);
                const void *src_element = descriptor_element(
                    sb, c->srcArrayElement + e, 0);
                if (dst_element && src_element)
                    memcpy(dst_element, src_element, copy_stride);
            }
        }
    }

    /* Descriptor arrays may be dereferenced through PTR_*TABLE metadata.
     * Publish all WC Garlic writes before command recording or submission. */
    vk_ps4_cpu_store_fence();
}

/* === vkCmdBindDescriptorSets === */
static void bind_descriptor_stage_tables(
    VkPs4CommandBuffer *cmd, GnmShaderStage stage,
    const GnmInputUsageSlot *slots, uint32_t slot_count,
    VkPs4DescriptorSet *const *sets, GnmBuffer *dynamic_table
) {
    for (uint32_t i = 0; i < slot_count; ++i) {
        const GnmInputUsageSlot *slot = &slots[i];
        void *table = NULL;
        if (slot->usagetype == GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE) {
            if (slot->apislot >= VK_PS4_MAX_DESCRIPTOR_SETS) {
                cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
                vk_ps4_log("descriptor: unsupported set %u in stage %u; draw rejected",
                           (unsigned)slot->apislot, (unsigned)stage);
                return;
            }
            VkPs4DescriptorSet *set = sets[slot->apislot];
            if (!set) {
                /* This runs right before the draw, so a missing set here is
                 * a shader about to read a table through an SGPR pair nothing
                 * has written: the silent GPU fault of B4 tests 10 and 11.
                 * Named once per hundred so a broken frame stays readable. */
                static unsigned missing_count = 0;
                if (missing_count++ % 100u == 0u)
                    vk_ps4_log("draw needs descriptor set %u (stage %u, reg %u) but none is bound (occurrence %u)",
                               (unsigned)slot->apislot, (unsigned)stage,
                               (unsigned)slot->startregister, missing_count);
                cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
                return;
            }
            if (!set->descriptor_mem.allocated || !set->descriptor_mem.mapped) {
                static unsigned unallocated_count = 0;
                if (unallocated_count++ % 100u == 0u)
                    vk_ps4_log("CmdBindDescriptorSets: set=%u stage=%u has no table memory (occurrence %u)",
                               (unsigned)slot->apislot, (unsigned)stage, unallocated_count);
                cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
                return;
            }
            table = set->descriptor_mem.mapped;
        } else if (slot->usagetype ==
                       GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE &&
                   slot->apislot ==
                       VK_PS4_PSBC_DYNAMIC_DESCRIPTOR_API_SLOT) {
            table = dynamic_table;
        } else {
            continue;
        }
        if (!table) {
            vk_ps4_log("descriptor: required table absent stage=%u type=%u slot=%u; draw rejected",
                       (unsigned)stage, (unsigned)slot->usagetype,
                       (unsigned)slot->apislot);
            cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        if ((uint32_t)((uint64_t)(uintptr_t)table >> 32u) !=
            VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI) {
            vk_ps4_log("CmdBindDescriptorSets: table address rejected set=%u ptr=%p",
                       (unsigned)slot->apislot, table);
            cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
            return;
        }
        vk_ps4_cpu_store_fence();
        /* RADV's AC_ARG_CONST_ADDR is one SGPR; address32_hi is compiled
         * into the shader. A GNM 64-bit pointer write overwrites the next
         * independent argument (B4 character VS: bones at s9 overwrote the
         * push-constant pointer at s10 with the value 2). Fetch/VB pointers
         * use a different, genuinely 64-bit ABI and retain their GNM calls. */
        vk_ps4_emit_address32_user_data(cmd, stage, slot->startregister, table);
        {
            static unsigned logged_binds = 0;
            if (logged_binds < 96u) {
                ++logged_binds;
                vk_ps4_log("bind[%u]: stage=%u slot-type=%u apislot=%u reg=%u table=%p",
                           logged_binds - 1u, (unsigned)stage, (unsigned)slot->usagetype,
                           (unsigned)slot->apislot, (unsigned)slot->startregister, table);
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                              VkPipelineLayout layout, uint32_t firstSet, uint32_t setCount,
                              const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
                              const uint32_t *pDynamicOffsets) {
    if (!commandBuffer || (setCount && !pDescriptorSets)) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4PipelineLayout *new_layout = (VkPs4PipelineLayout *)layout;
    if (!new_layout || firstSet > new_layout->set_layout_count ||
        setCount > new_layout->set_layout_count - firstSet)
        return;
    const bool compute = pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE;
    if (!compute)
        cmd->graphics_descriptor_layout = new_layout;

    VkPs4DescriptorSet **bound_sets =
        compute ? cmd->bound_compute_sets : cmd->bound_graphics_sets;
    uint32_t (*bound_offsets)[VK_PS4_MAX_DYNAMIC_DESCRIPTORS_PER_SET] =
        compute ? cmd->compute_dynamic_offsets : cmd->graphics_dynamic_offsets;

    /* B39: drop a bind that changes nothing.
     *
     * The renderers rebind the same sets constantly - terrain binds one
     * material set per chunk, the character pass rebinds set 0 and the bone
     * set for every batch of every instance, twice per instance because the
     * shadow pass repeats the loop. Each of those used to mark the tables
     * dirty, which forced the next draw to re-emit both the VS and PS table
     * pointers, and each one with dynamic descriptors also consumed a
     * snapshot from the command buffer's bounded dynamic-table arena. That
     * arena running out is a rejected recording, so this is not only cheaper
     * but removes a real failure mode in crowded scenes.
     *
     * Only the case where nothing at all differs is skipped: same layout,
     * same set handles, same dynamic offsets, and no pending dirty flag that
     * a draw has yet to consume. A set whose *contents* were updated between
     * two binds still needs the snapshot rebuilt, so any layout carrying
     * dynamic descriptors is excluded from the fast path entirely rather than
     * reasoned about. */
    const bool tables_dirty =
        compute ? cmd->compute_tables_dirty : cmd->graphics_tables_dirty;
    if (setCount > 0 && !tables_dirty && dynamicOffsetCount == 0 &&
        (compute || cmd->graphics_descriptor_layout == new_layout)) {
        bool identical = true;
        for (uint32_t si = 0; si < setCount && identical; ++si) {
            if (bound_sets[firstSet + si] !=
                (VkPs4DescriptorSet *)pDescriptorSets[si]) {
                identical = false;
            }
        }
        if (identical) {
            uint32_t layout_dynamic = 0;
            for (uint32_t set = 0; set < new_layout->set_layout_count; ++set) {
                const VkPs4DescriptorSetLayout *sl = new_layout->set_layouts[set];
                if (sl) layout_dynamic += sl->dynamic_descriptor_count;
            }
            if (layout_dynamic == 0) return;
        }
    }

    /* The table pointers are written before the next draw or dispatch, for
     * whichever pipeline is bound by then (vk_ps4_flush_descriptor_tables).
     * Vulkan allows the sets before the pipeline, and a pipeline change
     * moves them to the new shaders' registers; binding used to require the
     * pipeline first and dropped the sets otherwise, which is how the
     * character renderer's per-frame set never reached the console's first
     * scene draw (B4 test 11). */
    if (compute) cmd->compute_tables_dirty = true;
    else cmd->graphics_tables_dirty = true;
    if (setCount == 0) return; /* re-emission request: nothing new to record */

    uint32_t supplied_dynamic = 0;
    for (uint32_t si = 0; si < setCount; ++si) {
        const uint32_t set_number = firstSet + si;
        VkPs4DescriptorSet *set =
            (VkPs4DescriptorSet *)pDescriptorSets[si];
        bound_sets[set_number] = set;
        if (!set || !set->layout) continue;
        for (uint32_t bi = 0; bi < set->binding_count; ++bi) {
            VkPs4DescriptorBinding *binding = &set->bindings[bi];
            if (binding->type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
                binding->type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                continue;
            for (uint32_t e = 0; e < binding->count; ++e) {
                if (supplied_dynamic >= dynamicOffsetCount ||
                    !pDynamicOffsets) {
                    vk_ps4_log("CmdBindDescriptorSets: missing dynamic offset set=%u binding=%u",
                               set_number, binding->binding_number);
                    cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
                    return;
                }
                const uint32_t local = binding->dynamic_offset_offset + e;
                if (local >= VK_PS4_MAX_DYNAMIC_DESCRIPTORS_PER_SET) {
                    cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
                    return;
                }
                bound_offsets[set_number][local] =
                    pDynamicOffsets[supplied_dynamic++];
            }
        }
    }
    if (supplied_dynamic != dynamicOffsetCount) {
        vk_ps4_log("CmdBindDescriptorSets: dynamic offset count mismatch consumed=%u supplied=%u",
                   supplied_dynamic, dynamicOffsetCount);
        cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
        return;
    }

    uint32_t total_dynamic = 0;
    for (uint32_t set = 0; set < new_layout->set_layout_count; ++set) {
        const VkPs4DescriptorSetLayout *sl = new_layout->set_layouts[set];
        if (sl) total_dynamic += sl->dynamic_descriptor_count;
    }
    GnmBuffer *dynamic_table = NULL;
    if (total_dynamic) {
        if (total_dynamic > VK_PS4_MAX_DYNAMIC_DESCRIPTORS ||
            cmd->dynamic_table_cursor >=
                VK_PS4_MAX_DYNAMIC_TABLE_SNAPSHOTS ||
            !cmd->dynamic_descriptor_tables) {
            cmd->dynamic_table_overflow = true;
            return;
        }
        dynamic_table = cmd->dynamic_descriptor_tables +
            (size_t)cmd->dynamic_table_cursor *
            VK_PS4_MAX_DYNAMIC_DESCRIPTORS;
        cmd->dynamic_table_cursor++;
        memset(dynamic_table, 0, total_dynamic * sizeof(*dynamic_table));
        uint32_t set_dynamic_start = 0;
        for (uint32_t set_number = 0;
             set_number < new_layout->set_layout_count; ++set_number) {
            const VkPs4DescriptorSetLayout *sl =
                new_layout->set_layouts[set_number];
            VkPs4DescriptorSet *set = bound_sets[set_number];
            if (!sl) continue;
            if (set) {
                for (uint32_t bi = 0; bi < set->binding_count; ++bi) {
                    VkPs4DescriptorBinding *binding = &set->bindings[bi];
                    if (binding->type !=
                            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
                        binding->type !=
                            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
                        continue;
                    for (uint32_t e = 0; e < binding->count; ++e) {
                        GnmBuffer *src = buffer_element(binding, e);
                        const uint32_t local =
                            binding->dynamic_offset_offset + e;
                        if (!src || local >= sl->dynamic_descriptor_count)
                            continue;
                        GnmBuffer adjusted = *src;
                        char *base =
                            (char *)sceGnmBufGetBaseAddress(&adjusted);
                        sceGnmBufSetBaseAddress(
                            &adjusted, base +
                            bound_offsets[set_number][local]);
                        dynamic_table[set_dynamic_start + local] = adjusted;
                    }
                }
            }
            set_dynamic_start += sl->dynamic_descriptor_count;
        }
        /* The snapshot is read by the GPU through the pointer below. */
        vk_ps4_cpu_store_fence();
    }
    if (compute) cmd->compute_dynamic_table = dynamic_table;
    else cmd->graphics_dynamic_table = dynamic_table;
}

void vk_ps4_flush_descriptor_tables(VkPs4CommandBuffer *cmd, VkPipelineBindPoint bind_point) {
    if (!cmd) return;
    VkPs4Pipeline *pipe = cmd->current_pipeline;
    if (!pipe || pipe->bind_point != bind_point) return;
    if (bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) {
        if (!cmd->compute_tables_dirty) return;
        cmd->compute_tables_dirty = false;
        bind_descriptor_stage_tables(
            cmd, GNM_STAGE_CS, pipe->vs_input_usage_slots,
            pipe->vs_input_usage_slot_count, cmd->bound_compute_sets,
            cmd->compute_dynamic_table);
    } else {
        if (!cmd->graphics_tables_dirty) return;
        cmd->graphics_tables_dirty = false;
        bind_descriptor_stage_tables(
            cmd, GNM_STAGE_VS, pipe->vs_input_usage_slots,
            pipe->vs_input_usage_slot_count, cmd->bound_graphics_sets,
            cmd->graphics_dynamic_table);
        bind_descriptor_stage_tables(
            cmd, GNM_STAGE_PS, pipe->ps_input_usage_slots,
            pipe->ps_input_usage_slot_count, cmd->bound_graphics_sets,
            cmd->graphics_dynamic_table);
    }
}

/* === Sampler === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkSampler *pSampler) {
    if (!device || !pCreateInfo || !pSampler) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    GnmSampler *sampler = vk_ps4_alloc_zero(alloc, sizeof(*sampler), 16);
    if (!sampler) return VK_ERROR_OUT_OF_HOST_MEMORY;

    build_sampler_descriptor(sampler, pCreateInfo);

    *pSampler = (VkSampler)sampler;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator) {
    if (!device || !sampler) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, (void *)sampler);
}
