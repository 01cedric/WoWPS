/*
 * vk_ps4_pipeline.c — VkPipeline implementation.
 *
 * vkCreateGraphicsPipelines compiles shader stages via libpsbc,
 * extracts GNM stage registers from the shader binary, and stores
 * pipeline state (blend, rasterizer, depth/stencil) for later
 * emission in vkCmdBindPipeline.
 */

#include "vk_ps4_internal.h"
#include "vk_ps4_compute_shader.h"
#include "vk_ps4_shader_linkage.h"

#include <string.h>

/* PM4 register definitions for stencil op/ref/mask programming. */
#include <pm4/sid.h>
#include <pm4/amdgfxregs.h>

/* Forward declaration from vk_ps4_shader.c */
VkResult vk_ps4_compile_shader_module(VkPs4ShaderModule *mod, VkShaderStageFlagBits stage,
                                      const VkPs4PipelineLayout *layout,
                                      const VkAllocationCallbacks *alloc,
                                      void **out_binary, size_t *out_binary_size,
                                      GnmShaderMetadata *out_metadata);

/* psbc's generic vertex prolog starts with an SOP1 SWAPPC call into the
 * OpenGNM fetch shader.  The compiler currently emits the call source as
 * s[0:1] even though its own input-usage metadata places the fetch pointer in
 * another user-SGPR pair (s[2:3] for the M6.2 shaders).  s[0:1] is the return
 * address written by SWAPPC, so changing the PM4 binding to slot zero is not a
 * valid workaround.  Relocate only the source byte of the verified prolog
 * instruction before executable bytes are copied to Garlic. */
#define VK_PS4_PSBC_VS_SWAPPCCALL_BASE 0xbe802100u

/* GFX7 EXP instructions are 64-bit and carry the 6-bit opcode in the high
 * bits of their first dword.  Count them so metadata cannot claim raster
 * varyings that the standalone PSBC compiler silently omitted from ISA. */
static uint32_t vk_ps4_count_gcn_exp_instructions(const void *code,
                                                   uint32_t code_size)
{
    if (!code || code_size < sizeof(uint32_t))
        return 0;
    const uint32_t *words = (const uint32_t *)code;
    const uint32_t count = code_size / sizeof(uint32_t);
    uint32_t exports = 0;
    for (uint32_t index = 0; index < count; ++index) {
        if ((words[index] & 0xfc000000u) == 0xf8000000u)
            ++exports;
    }
    return exports;
}
#define VK_PS4_VS_USER_SGPR_COUNT 16u

static uint64_t vk_ps4_pipeline_layout_hash(
    uint64_t hash, const VkPs4PipelineLayout *layout
) {
    const uint64_t prime = UINT64_C(1099511628211);
#define HASH_U32(value_) do { \
        uint32_t hash_value_ = (uint32_t)(value_); \
        for (uint32_t hash_byte_ = 0; hash_byte_ < 4; ++hash_byte_) { \
            hash ^= (hash_value_ >> (hash_byte_ * 8)) & 0xffu; \
            hash *= prime; \
        } \
    } while (0)
    HASH_U32(layout ? layout->set_layout_count : 0u);
    if (layout) {
        for (uint32_t set = 0; set < layout->set_layout_count; ++set) {
            const VkPs4DescriptorSetLayout *sl = layout->set_layouts[set];
            HASH_U32(sl ? sl->binding_count : 0u);
            if (!sl) continue;
            for (uint32_t i = 0; i < sl->binding_count; ++i) {
                HASH_U32(sl->bindings[i].binding);
                HASH_U32(sl->bindings[i].descriptorType);
                HASH_U32(sl->bindings[i].descriptorCount);
                HASH_U32(sl->bindings[i].stageFlags);
                HASH_U32(sl->binding_offsets[i]);
                HASH_U32(sl->binding_strides[i]);
                HASH_U32(sl->binding_dynamic_offsets[i]);
            }
        }
    }
#undef HASH_U32
    return hash;
}

typedef struct VkPs4ShaderUploadPatch {
    bool enabled;
    uint32_t old_word;
    uint32_t new_word;
    uint32_t fetch_slot;
} VkPs4ShaderUploadPatch;

static VkResult vk_ps4_pipeline_prepare_vs_fetch_relocation(
    VkShaderStageFlagBits stage, const GnmShaderMetadata *metadata,
    VkPs4ShaderUploadPatch *patch
) {
    if (!metadata || !patch) return VK_ERROR_INVALID_SHADER_NV;
    memset(patch, 0, sizeof(*patch));

    if (stage != VK_SHADER_STAGE_VERTEX_BIT ||
        metadata->type != GNM_SHADER_VERTEX) {
        return VK_SUCCESS;
    }

    const GnmVsShader *vs = (const GnmVsShader *)metadata->stage;
    if (!vs || vs->numinputsemantics == 0) {
        return VK_SUCCESS;
    }

    if (!metadata->inputusageslots || metadata->numinputusageslots == 0 ||
        !metadata->shadercode || metadata->shadercodesize < sizeof(uint32_t)) {
        vk_ps4_log_raw("pipeline: VS fetch prolog REJECT missing metadata/code");
        return VK_ERROR_INVALID_SHADER_NV;
    }

    uint32_t fetch_slot = 0;
    uint32_t fetch_slot_count = 0;
    for (uint32_t i = 0; i < metadata->numinputusageslots; i++) {
        if (metadata->inputusageslots[i].usagetype ==
            GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER) {
            fetch_slot = metadata->inputusageslots[i].startregister;
            fetch_slot_count++;
        }
    }

    if (fetch_slot_count != 1 || (fetch_slot & 1u) != 0 ||
        fetch_slot + 1u >= VK_PS4_VS_USER_SGPR_COUNT) {
        vk_ps4_log("pipeline: VS fetch prolog REJECT slots=%u start=%u",
                   fetch_slot_count, fetch_slot);
        return VK_ERROR_INVALID_SHADER_NV;
    }

    uint32_t old_word = 0;
    memcpy(&old_word, metadata->shadercode, sizeof(old_word));
    if ((old_word & 0xffffff00u) != VK_PS4_PSBC_VS_SWAPPCCALL_BASE) {
        vk_ps4_log("pipeline: VS fetch prolog REJECT word=0x%08x slot=%u",
                   old_word, fetch_slot);
        return VK_ERROR_INVALID_SHADER_NV;
    }

    patch->old_word = old_word;
    patch->new_word = (old_word & 0xffffff00u) | fetch_slot;
    patch->fetch_slot = fetch_slot;
    patch->enabled = patch->new_word != patch->old_word;
    vk_ps4_log("pipeline: VS fetch prolog relocated old=0x%08x new=0x%08x slot=%u apply=%u",
               patch->old_word, patch->new_word, patch->fetch_slot,
               patch->enabled ? 1u : 0u);
    return VK_SUCCESS;
}

/* Select the executable allocation owned by a pipeline stage. */
static GnmDirectMemory *vk_ps4_pipeline_stage_code_mem(
    VkPs4Pipeline *pipe, VkShaderStageFlagBits stage
) {
    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT: return &pipe->vs_code_mem;
    case VK_SHADER_STAGE_FRAGMENT_BIT: return &pipe->ps_code_mem;
    case VK_SHADER_STAGE_GEOMETRY_BIT: return &pipe->gs_code_mem;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return &pipe->tcs_code_mem;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return &pipe->tes_code_mem;
    case VK_SHADER_STAGE_COMPUTE_BIT: return &pipe->cs_code_mem;
    default: return NULL;
    }
}

/* Copy executable shader bytes into a GPU-owned, 256-byte-safe address.
 * GCN's SPI/COMPUTE_PGM_LO registers encode address >> 8.  Pointing them at
 * metadata.shadercode inside a normally aligned malloc block silently drops
 * meaningful low address bits and can make the GPU execute container/header
 * bytes.  A 64 KiB-aligned direct allocation removes that ambiguity and also
 * guarantees instruction fetch visibility on real PS4 hardware. */
static VkResult vk_ps4_pipeline_upload_shader_code(
    VkPs4Pipeline *pipe, VkShaderStageFlagBits stage,
    const void *code, uint32_t code_size,
    const VkPs4ShaderUploadPatch *patch, void **out_gpu_code
) {
    if (!pipe || !code || code_size == 0 || !out_gpu_code) {
        return VK_ERROR_INVALID_SHADER_NV;
    }
#if defined(__ORBIS__) || defined(__PS4__)
    GnmDirectMemory *mem = vk_ps4_pipeline_stage_code_mem(pipe, stage);
    if (!mem || mem->allocated) return VK_ERROR_INITIALIZATION_FAILED;
    GnmError err = sceGnmDirectMemoryAllocate(
        mem, code_size, 64u * 1024u,
        GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
    );
    if (err != GNM_ERROR_OK || !mem->mapped) {
        vk_ps4_log("pipeline: shader stage=0x%x Garlic upload FAILED rc=%d size=%u",
                   (unsigned)stage, (int)err, code_size);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    memcpy(mem->mapped, code, code_size);
    if (patch && patch->enabled) {
        memcpy(mem->mapped, &patch->new_word, sizeof(patch->new_word));
    }
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
    __asm__ volatile("sfence" ::: "memory");
#endif
    *out_gpu_code = mem->mapped;
    vk_ps4_log("pipeline: shader stage=0x%x exec=%p size=%u aligned256=%u",
               (unsigned)stage, mem->mapped, code_size,
               (unsigned)(((uintptr_t)mem->mapped & 255u) == 0));
#else
    /* Generic builds never execute PM4.  Preserve their lightweight test
     * behavior without requiring an emulated direct-memory allocator. */
    (void)pipe;
    (void)stage;
    (void)patch;
    *out_gpu_code = (void *)code;
#endif
    return VK_SUCCESS;
}

static void vk_ps4_pipeline_release_shader_code(VkPs4Pipeline *pipe) {
    if (!pipe) return;
    GnmDirectMemory *memories[] = {
        &pipe->vs_code_mem, &pipe->ps_code_mem, &pipe->gs_code_mem,
        &pipe->tcs_code_mem, &pipe->tes_code_mem, &pipe->cs_code_mem,
    };
    for (uint32_t i = 0; i < sizeof(memories) / sizeof(memories[0]); i++) {
        if (memories[i]->allocated) sceGnmDirectMemoryRelease(memories[i]);
        memset(memories[i], 0, sizeof(*memories[i]));
    }
}

/* Forward declaration from vk_ps4_command.c */
extern uint32_t vk_stencil_op_to_pm4(VkStencilOp op);

/* === Blend state conversion === */

static GnmBlendOp vk_blend_factor_to_gnm(VkBlendFactor f) {
    switch (f) {
    case VK_BLEND_FACTOR_ZERO:                       return GNM_BLEND_ZERO;
    case VK_BLEND_FACTOR_ONE:                        return GNM_BLEND_ONE;
    case VK_BLEND_FACTOR_SRC_COLOR:                  return GNM_BLEND_SRC_COLOR;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:        return GNM_BLEND_ONE_MINUS_SRC_COLOR;
    case VK_BLEND_FACTOR_DST_COLOR:                  return GNM_BLEND_DEST_COLOR;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:        return GNM_BLEND_ONE_MINUS_DEST_COLOR;
    case VK_BLEND_FACTOR_SRC_ALPHA:                  return GNM_BLEND_SRC_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:        return GNM_BLEND_ONE_MINUS_SRC_ALPHA;
    case VK_BLEND_FACTOR_DST_ALPHA:                  return GNM_BLEND_DEST_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:        return GNM_BLEND_ONE_MINUS_DEST_ALPHA;
    case VK_BLEND_FACTOR_CONSTANT_COLOR:             return GNM_BLEND_CONSTANT_COLOR;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:   return GNM_BLEND_ONE_MINUS_CONSTANT_COLOR;
    case VK_BLEND_FACTOR_CONSTANT_ALPHA:             return GNM_BLEND_CONSTANT_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:   return GNM_BLEND_ONE_MINUS_CONSTANT_ALPHA;
    case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:         return GNM_BLEND_SRC_ALPHA_SATURATE;
    case VK_BLEND_FACTOR_SRC1_COLOR:                 return GNM_BLEND_SRC1_COLOR;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:       return GNM_BLEND_INVERSE_SRC1_COLOR;
    case VK_BLEND_FACTOR_SRC1_ALPHA:                 return GNM_BLEND_SRC1_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:       return GNM_BLEND_INVERSE_SRC1_ALPHA;
    default:                                         return GNM_BLEND_ZERO;
    }
}

static GnmCombFunc vk_blend_op_to_gnm(VkBlendOp op) {
    switch (op) {
    case VK_BLEND_OP_ADD:                 return GNM_COMB_DST_PLUS_SRC;
    case VK_BLEND_OP_SUBTRACT:            return GNM_COMB_SRC_MINUS_DST;
    case VK_BLEND_OP_REVERSE_SUBTRACT:    return GNM_COMB_DST_MINUS_SRC;
    case VK_BLEND_OP_MIN:                 return GNM_COMB_MIN_DST_SRC;
    case VK_BLEND_OP_MAX:                 return GNM_COMB_MAX_DST_SRC;
    default:                              return GNM_COMB_DST_PLUS_SRC;
    }
}

/* Convert a VkPipelineColorBlendAttachmentState to GnmBlendControl. */
static void vk_blend_attachment_to_gnm(const VkPipelineColorBlendAttachmentState *att,
                                        GnmBlendControl *out) {
    memset(out, 0, sizeof(*out));
    out->blendenabled = att->blendEnable ? true : false;
    out->colorfunc = vk_blend_op_to_gnm(att->colorBlendOp);
    out->colorsrcmult = vk_blend_factor_to_gnm(att->srcColorBlendFactor);
    out->colordstmult = vk_blend_factor_to_gnm(att->dstColorBlendFactor);
    out->alphafunc = vk_blend_op_to_gnm(att->alphaBlendOp);
    out->alphasrcmult = vk_blend_factor_to_gnm(att->srcAlphaBlendFactor);
    out->alphadstmult = vk_blend_factor_to_gnm(att->dstAlphaBlendFactor);
    out->separatealphaenable = (att->alphaBlendOp != att->colorBlendOp ||
        att->srcAlphaBlendFactor != att->srcColorBlendFactor ||
        att->dstAlphaBlendFactor != att->dstColorBlendFactor);
}

GnmPrimitiveType vk_topology_to_gnm(VkPrimitiveTopology topology) {
    switch (topology) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return GNM_PT_POINTLIST;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return GNM_PT_LINELIST;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: return GNM_PT_LINESTRIP;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: return GNM_PT_TRILIST;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return GNM_PT_TRISTRIP;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN: return GNM_PT_TRIFAN;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return GNM_PT_LINELIST_ADJ;
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return GNM_PT_LINESTRIP_ADJ;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return GNM_PT_TRILIST_ADJ;
    case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return GNM_PT_TRIPSTRIP_ADJ;
    case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST: return GNM_PT_TRILIST; /* tess uses different path */
    default: return GNM_PT_TRILIST;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                                uint32_t createInfoCount,
                                const VkGraphicsPipelineCreateInfo *pCreateInfos,
                                const VkAllocationCallbacks *pAllocator,
                                VkPipeline *pPipelines) {
    VK_PS4_LOG_ENTRY();
    vk_ps4_log("CreateGraphicsPipelines: count=%u", createInfoCount);

    if (!device || !pCreateInfos || !pPipelines) {
        vk_ps4_log_raw("CreateGraphicsPipelines: NULL args, FAIL");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkResult overall_result = VK_SUCCESS;

    for (uint32_t i = 0; i < createInfoCount; i++) {
        const VkGraphicsPipelineCreateInfo *ci = &pCreateInfos[i];
        VkPs4Pipeline *pipe = vk_ps4_alloc_zero(alloc, sizeof(*pipe), 16);
        if (!pipe) {
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_OUT_OF_HOST_MEMORY;
            continue;
        }
        pipe->type = VK_PS4_OBJ_PIPELINE;
        pipe->device = dev;
        pipe->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;

        /* Zero out stage registers */
        memset(&pipe->vs_regs, 0, sizeof(pipe->vs_regs));
        memset(&pipe->ps_regs, 0, sizeof(pipe->ps_regs));
        memset(&pipe->cs_regs, 0, sizeof(pipe->cs_regs));

        /* Store pipeline state — deep copy vertex input state to avoid
         * dangling pointers (app can free pVertexInputState after creation). */
        if (ci->pVertexInputState) {
            pipe->vertex_input_state = *ci->pVertexInputState;
            const VkPipelineVertexInputStateCreateInfo *vi = ci->pVertexInputState;
            /* Deep copy vertex binding descriptions */
            if (vi->vertexBindingDescriptionCount > 0 && vi->pVertexBindingDescriptions) {
                uint32_t n = vi->vertexBindingDescriptionCount;
                pipe->vertex_bindings = vk_ps4_alloc_zero(alloc,
                    n * sizeof(VkVertexInputBindingDescription), 16);
                if (pipe->vertex_bindings) {
                    memcpy(pipe->vertex_bindings, vi->pVertexBindingDescriptions,
                           n * sizeof(VkVertexInputBindingDescription));
                    pipe->vertex_input_state.pVertexBindingDescriptions = pipe->vertex_bindings;
                } else {
                    /* Alloc failure — zero out to avoid dangling pointer */
                    pipe->vertex_input_state.vertexBindingDescriptionCount = 0;
                    pipe->vertex_input_state.pVertexBindingDescriptions = NULL;
                }
            } else {
                pipe->vertex_input_state.vertexBindingDescriptionCount = 0;
                pipe->vertex_input_state.pVertexBindingDescriptions = NULL;
            }
            /* Deep copy vertex attribute descriptions */
            if (vi->vertexAttributeDescriptionCount > 0 && vi->pVertexAttributeDescriptions) {
                uint32_t n = vi->vertexAttributeDescriptionCount;
                pipe->vertex_attributes = vk_ps4_alloc_zero(alloc,
                    n * sizeof(VkVertexInputAttributeDescription), 16);
                if (pipe->vertex_attributes) {
                    memcpy(pipe->vertex_attributes, vi->pVertexAttributeDescriptions,
                           n * sizeof(VkVertexInputAttributeDescription));
                    pipe->vertex_input_state.pVertexAttributeDescriptions = pipe->vertex_attributes;
                } else {
                    pipe->vertex_input_state.vertexAttributeDescriptionCount = 0;
                    pipe->vertex_input_state.pVertexAttributeDescriptions = NULL;
                }
            } else {
                pipe->vertex_input_state.vertexAttributeDescriptionCount = 0;
                pipe->vertex_input_state.pVertexAttributeDescriptions = NULL;
            }
        }
        if (ci->pInputAssemblyState)
            pipe->input_assembly_state = *ci->pInputAssemblyState;
        /* Tessellation state: patch control points */
        if (ci->pTessellationState && ci->pTessellationState->patchControlPoints > 0) {
            pipe->tess_patch_control_points = ci->pTessellationState->patchControlPoints;
        }
        if (ci->pRasterizationState)
            pipe->rasterization_state = *ci->pRasterizationState;
        if (ci->pColorBlendState) {
            pipe->color_blend_state = *ci->pColorBlendState;
            pipe->has_blend_state = true;

            /* Deep copy blend attachment states (pAttachments is a pointer
             * to caller-owned memory that may be freed after pipeline creation). */
            const VkPipelineColorBlendStateCreateInfo *cb = ci->pColorBlendState;
            if (cb->attachmentCount > 0 && cb->pAttachments) {
                uint32_t n = cb->attachmentCount;
                if (n > 8) n = 8;  /* max 8 RT slots */
                pipe->blend_attachments = vk_ps4_alloc_zero(alloc,
                    n * sizeof(VkPipelineColorBlendAttachmentState), 16);
                if (pipe->blend_attachments) {
                    memcpy(pipe->blend_attachments, cb->pAttachments,
                           n * sizeof(VkPipelineColorBlendAttachmentState));
                    pipe->color_blend_state.pAttachments = pipe->blend_attachments;
                    pipe->color_blend_state.attachmentCount = n;

                    /* Pre-compute GnmBlendControl for each RT slot */
                    pipe->blend_control_count = n;
                    pipe->color_write_mask = 0;
                    for (uint32_t j = 0; j < n; j++) {
                        vk_blend_attachment_to_gnm(&cb->pAttachments[j],
                            &pipe->blend_controls[j]);
                        /* Pack colorWriteMask into 4-bit-per-RT mask */
                        uint32_t wm = cb->pAttachments[j].colorWriteMask & 0xF;
                        pipe->color_write_mask |= wm << (j * 4);
                    }
                } else {
                    pipe->color_blend_state.pAttachments = NULL;
                    pipe->color_blend_state.attachmentCount = 0;
                    pipe->blend_control_count = 0;
                    pipe->color_write_mask = 0xFFFFFFFF;  /* write all by default */
                }
            } else {
                pipe->color_blend_state.pAttachments = NULL;
                pipe->color_blend_state.attachmentCount = 0;
                pipe->blend_control_count = 0;
                pipe->color_write_mask = 0xFFFFFFFF;  /* write all by default */
            }

            /* Copy blend constants */
            memcpy(pipe->blend_constants, cb->blendConstants, sizeof(pipe->blend_constants));
        } else {
            pipe->has_blend_state = false;
            pipe->color_write_mask = 0xFFFFFFFF;  /* write all by default */
        }
        if (ci->pDepthStencilState) {
            pipe->depth_stencil_state = *ci->pDepthStencilState;
            pipe->has_depth_stencil_state = true;

            /* Convert VkPipelineDepthStencilStateCreateInfo to GNM state */
            const VkPipelineDepthStencilStateCreateInfo *ds = ci->pDepthStencilState;
            GnmDepthStencilControl *dsc = &pipe->depth_stencil_control;
            memset(dsc, 0, sizeof(*dsc));

            dsc->depthenable = ds->depthTestEnable;
            dsc->zwrite = ds->depthWriteEnable;
            dsc->zfunc = (GnmDepthCompare)ds->depthCompareOp;
            dsc->depthboundsenable = ds->depthBoundsTestEnable;
            dsc->stencilenable = ds->stencilTestEnable;
            dsc->separatestencilenable = false;

            if (ds->stencilTestEnable) {
                /* Front face stencil */
                dsc->stencilfunc = (GnmDepthCompare)ds->front.compareOp;
                /* Check if back face is different from front */
                if (ds->back.compareOp != ds->front.compareOp ||
                    ds->back.failOp != ds->front.failOp ||
                    ds->back.passOp != ds->front.passOp ||
                    ds->back.depthFailOp != ds->front.depthFailOp ||
                    ds->back.compareMask != ds->front.compareMask ||
                    ds->back.writeMask != ds->front.writeMask ||
                    ds->back.reference != ds->front.reference) {
                    dsc->separatestencilenable = true;
                    dsc->stencilbackfunc = (GnmDepthCompare)ds->back.compareOp;
                }

                /* Pre-compute DB_STENCIL_CONTROL (ops for front and back) */
                pipe->stencil_control =
                    S_02842C_STENCILFAIL(vk_stencil_op_to_pm4(ds->front.failOp)) |
                    S_02842C_STENCILZPASS(vk_stencil_op_to_pm4(ds->front.passOp)) |
                    S_02842C_STENCILZFAIL(vk_stencil_op_to_pm4(ds->front.depthFailOp));
                if (dsc->separatestencilenable) {
                    pipe->stencil_control |=
                        S_02842C_STENCILFAIL_BF(vk_stencil_op_to_pm4(ds->back.failOp)) |
                        S_02842C_STENCILZPASS_BF(vk_stencil_op_to_pm4(ds->back.passOp)) |
                        S_02842C_STENCILZFAIL_BF(vk_stencil_op_to_pm4(ds->back.depthFailOp));
                }

                /* Pre-compute DB_STENCILREFMASK (front) */
                pipe->stencil_refmask =
                    S_028430_STENCILTESTVAL(ds->front.reference) |
                    S_028430_STENCILMASK(ds->front.compareMask) |
                    S_028430_STENCILWRITEMASK(ds->front.writeMask);

                /* Pre-compute DB_STENCILREFMASK_BF (back) */
                pipe->stencil_refmask_bf =
                    S_028434_STENCILTESTVAL_BF(ds->back.reference) |
                    S_028434_STENCILMASK_BF(ds->back.compareMask) |
                    S_028434_STENCILWRITEMASK_BF(ds->back.writeMask);
            }
        }
        if (ci->pViewportState) {
            pipe->viewport_state = *ci->pViewportState;
            pipe->static_viewport_count = ci->pViewportState->viewportCount;
            if (pipe->static_viewport_count > VK_PS4_MAX_VIEWPORTS)
                pipe->static_viewport_count = VK_PS4_MAX_VIEWPORTS;
            if (ci->pViewportState->pViewports &&
                pipe->static_viewport_count > 0) {
                memcpy(pipe->static_viewports,
                       ci->pViewportState->pViewports,
                       pipe->static_viewport_count * sizeof(VkViewport));
                pipe->viewport_state.pViewports = pipe->static_viewports;
                pipe->viewport_state.viewportCount =
                    pipe->static_viewport_count;
            } else {
                pipe->static_viewport_count = 0;
                pipe->viewport_state.pViewports = NULL;
            }
            pipe->static_scissor_count = ci->pViewportState->scissorCount;
            if (pipe->static_scissor_count > VK_PS4_MAX_VIEWPORTS)
                pipe->static_scissor_count = VK_PS4_MAX_VIEWPORTS;
            if (ci->pViewportState->pScissors &&
                pipe->static_scissor_count > 0) {
                memcpy(pipe->static_scissors,
                       ci->pViewportState->pScissors,
                       pipe->static_scissor_count * sizeof(VkRect2D));
                pipe->viewport_state.pScissors = pipe->static_scissors;
                pipe->viewport_state.scissorCount =
                    pipe->static_scissor_count;
            } else {
                pipe->static_scissor_count = 0;
                pipe->viewport_state.pScissors = NULL;
            }
        }
        if (ci->pDynamicState && ci->pDynamicState->pDynamicStates) {
            for (uint32_t d = 0;
                 d < ci->pDynamicState->dynamicStateCount; d++) {
                switch (ci->pDynamicState->pDynamicStates[d]) {
                case VK_DYNAMIC_STATE_VIEWPORT:
                    pipe->dynamic_viewport = true;
                    break;
                case VK_DYNAMIC_STATE_SCISSOR:
                    pipe->dynamic_scissor = true;
                    break;
                case VK_DYNAMIC_STATE_DEPTH_BIAS:
                    pipe->dynamic_depth_bias = true;
                    break;
                case VK_DYNAMIC_STATE_LINE_WIDTH:
                    pipe->dynamic_line_width = true;
                    break;
                default:
                    break;
                }
            }
        }
        if (ci->pMultisampleState)
            pipe->multisample_state = *ci->pMultisampleState;

        /* Process shader stages */
        bool compile_ok = true;
        bool vs_found = false, ps_found = false;
        for (uint32_t s = 0; s < ci->stageCount; s++) {
            const VkPipelineShaderStageCreateInfo *stage = &ci->pStages[s];
            VkPs4ShaderModule *mod = (VkPs4ShaderModule *)stage->module;

            void *binary = NULL;
            size_t binary_size = 0;
            GnmShaderMetadata metadata = {0};

            /* Check pipeline cache first — avoid recompiling if cached */
            uint64_t cache_hash = 0;
            size_t cached_size = 0;
            void *cached_binary = NULL;
            if (pipelineCache && mod->binary && mod->binary_size > 0) {
                cache_hash = vk_ps4_pipeline_cache_hash(mod->binary, mod->binary_size,
                                                        (uint32_t)stage->stage);
                cache_hash = vk_ps4_pipeline_layout_hash(
                    cache_hash, (const VkPs4PipelineLayout *)ci->layout);
                cached_binary = vk_ps4_pipeline_cache_lookup(pipelineCache, cache_hash,
                                                             (uint32_t)stage->stage,
                                                             &cached_size);
            }

            if (cached_binary && cached_size > 0) {
                /* Cache hit — copy the binary (we need our own copy because
                 * the cache may be destroyed before the pipeline) */
                binary = vk_ps4_alloc(alloc, cached_size, 16);
                if (binary) {
                    memcpy(binary, cached_binary, cached_size);
                    binary_size = cached_size;
                    /* Parse metadata from the cached binary */
                    sceGnmShaderBinaryGetMetadata(binary, binary_size, &metadata);
                }
            } else {
                /* Cache miss — compile the shader */
                VkResult vr = vk_ps4_compile_shader_module(
                    mod, stage->stage, (const VkPs4PipelineLayout *)ci->layout,
                    alloc, &binary, &binary_size, &metadata
                );
                if (vr != VK_SUCCESS) {
                    if (binary && binary != mod->binary) {
                        vk_ps4_free(alloc, binary);
                    }
                    compile_ok = false;
                    break;
                }
                /* Insert into pipeline cache */
                if (pipelineCache && binary && binary_size > 0 && mod->binary && mod->binary_size > 0) {
                    vk_ps4_pipeline_cache_insert(pipelineCache, cache_hash,
                                                 (uint32_t)stage->stage,
                                                 (uint32_t)mod->binary_size,
                                                 binary, binary_size);
                }
            }

            /* Metadata tables may remain in the compiler container, but the
             * executable bytes must live at a GCN-safe GPU address. */
            void *gpu_code = NULL;
            if (!metadata.fileheader || !metadata.stage ||
                !metadata.shadercode || metadata.shadercodesize == 0) {
                vk_ps4_log("CreateGraphicsPipelines: stage=0x%x metadata/code missing",
                           (unsigned)stage->stage);
                if (binary && binary != mod->binary) vk_ps4_free(alloc, binary);
                compile_ok = false;
                break;
            }
            VkPs4ShaderUploadPatch upload_patch = {0};
            VkResult relocation_result =
                vk_ps4_pipeline_prepare_vs_fetch_relocation(
                    stage->stage, &metadata, &upload_patch
                );
            if (relocation_result != VK_SUCCESS) {
                if (binary && binary != mod->binary) vk_ps4_free(alloc, binary);
                compile_ok = false;
                break;
            }
            VkResult upload_result = vk_ps4_pipeline_upload_shader_code(
                pipe, stage->stage, metadata.shadercode,
                metadata.shadercodesize, &upload_patch, &gpu_code
            );
            if (upload_result != VK_SUCCESS) {
                if (binary && binary != mod->binary) vk_ps4_free(alloc, binary);
                compile_ok = false;
                break;
            }

            /* Extract stage registers and input usage slots from the compiled shader binary */
            if (metadata.fileheader && metadata.stage) {
                /* Extract input usage slots (shared across all stages in the binary) */
                uint32_t nslots = metadata.numinputusageslots;
                if (nslots > VK_PS4_MAX_INPUT_USAGE_SLOTS) nslots = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                const GnmInputUsageSlot *slots = metadata.inputusageslots;

                switch (stage->stage) {
                case VK_SHADER_STAGE_VERTEX_BIT: {
                    /* The shader compiler may output different binary types
                     * depending on the pipeline configuration:
                     * - GNM_SHADER_VERTEX: standard VS (VS_VS)
                     * - GNM_SHADER_LOCAL: LS (VS before tessellation)
                     * - GNM_SHADER_EXPORT: ES (VS before geometry shader)
                     * All three start with GnmShaderCommonData, but the
                     * stage registers differ. */
                    const uint8_t *stage_ptr = (const uint8_t *)metadata.stage;
                    if (metadata.type == GNM_SHADER_LOCAL) {
                        /* LS: GnmShaderCommonData + GnmLsStageRegisters */
                        const GnmLsStageRegisters *ls_regs =
                            (const GnmLsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->ls_regs = *ls_regs;
                        pipe->has_ls = true;
                        /* Patch the shader code address from file-offset
                         * to actual GPU address in the compiled binary. */
                        if (gpu_code) {
                            sceGnmLsRegsSetAddress(&pipe->ls_regs,
                                gpu_code);
                        }
                        /* LS doesn't have vertex input semantics in the same
                         * format — skip semantic extraction for LS */
                    } else if (metadata.type == GNM_SHADER_EXPORT) {
                        /* ES: GnmShaderCommonData + GnmEsStageRegisters */
                        const GnmEsStageRegisters *es_regs =
                            (const GnmEsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->es_regs = *es_regs;
                        pipe->has_es = true;
                        if (gpu_code) {
                            sceGnmEsRegsSetAddress(&pipe->es_regs,
                                gpu_code);
                        }
                    } else {
                        /* Standard VS: GnmVsShader (common + regs + semantics) */
                        const GnmVsShader *vs = (const GnmVsShader *)metadata.stage;
                        pipe->vs_regs = vs->registers;
                        if (gpu_code) {
                            sceGnmVsRegsSetAddress(&pipe->vs_regs,
                                gpu_code);
                        }
                        /* Extract vertex input semantics */
                        uint32_t nsemantics = vs->numinputsemantics;
                        if (nsemantics > VK_PS4_MAX_INPUT_USAGE_SLOTS) nsemantics = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                        const GnmVertexInputSemantic *semantics = sceGnmVsShaderInputSemanticTable(vs);
                        if (semantics && nsemantics > 0) {
                            memcpy(pipe->vs_input_semantics, semantics, nsemantics * sizeof(GnmVertexInputSemantic));
                            pipe->vs_input_semantic_count = nsemantics;
                        }
                        uint32_t nexports = vs->numexportsemantics;
                        if (nexports > VK_PS4_MAX_INPUT_USAGE_SLOTS)
                            nexports = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                        const GnmVertexExportSemantic *exports =
                            sceGnmVsShaderExportSemanticTable(vs);
                        if (exports && nexports > 0) {
                            memcpy(pipe->vs_export_semantics, exports,
                                   nexports * sizeof(GnmVertexExportSemantic));
                            pipe->vs_export_semantic_count = nexports;
                        }
                        const uint32_t isa_exports =
                            vk_ps4_count_gcn_exp_instructions(
                                metadata.shadercode, metadata.shadercodesize);
                        /* POS0 is mandatory, followed by one PARAM export for
                         * each generic vertex export semantic.  The old PSBC
                         * output advertised UV PARAM0 in metadata but emitted
                         * only POS0, producing random atlas coordinates. */
                        if (isa_exports < nexports + 1u) {
                            vk_ps4_log("pipeline: VS raster ISA REJECT code-exports=%u required=%u metadata=%u",
                                       isa_exports, nexports + 1u, nexports);
                            compile_ok = false;
                            break;
                        }
                        vk_ps4_log("pipeline: VS raster ISA exports=%u required=%u validated=1",
                                   isa_exports, nexports + 1u);
                    }
                    pipe->vs_module = mod;
                    vs_found = true;
                    if (slots && nslots > 0) {
                        memcpy(pipe->vs_input_usage_slots, slots, nslots * sizeof(GnmInputUsageSlot));
                        pipe->vs_input_usage_slot_count = nslots;
                    }
                    break;
                }
                case VK_SHADER_STAGE_FRAGMENT_BIT: {
                    const GnmPsShader *ps = (const GnmPsShader *)metadata.stage;
                    /*
                     * psbc's generic metadata helper does not publish the PS
                     * input-usage table for the small sampler-only shader on
                     * retail FW 5.05.  The stage header is authoritative and
                     * still contains both PTR_RESOURCETABLE and
                     * PTR_SAMPLERTABLE.  Falling back to the shared metadata
                     * therefore leaves CmdBindDescriptorSets with zero PS
                     * slots and every texture lookup returns black.
                     *
                     * Always read the fragment-stage table from GnmPsShader.
                     * Keep the generic table only as a consistency breadcrumb;
                     * it may legitimately be empty for this compiler output.
                     */
                    uint32_t ps_nslots = ps->common.numinputusageslots;
                    if (ps_nslots > VK_PS4_MAX_INPUT_USAGE_SLOTS)
                        ps_nslots = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                    const GnmInputUsageSlot *ps_slots =
                        sceGnmPsShaderInputUsageSlotTable(ps);
                    pipe->ps_regs = ps->registers;
                    if (gpu_code) {
                        sceGnmPsRegsSetAddress(&pipe->ps_regs,
                            gpu_code);
                    }
                    pipe->fs_module = mod;
                    ps_found = true;
                    pipe->has_ps = true;
                    uint32_t nsemantics = ps->numinputsemantics;
                    if (nsemantics > VK_PS4_MAX_INPUT_USAGE_SLOTS)
                        nsemantics = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                    const GnmPixelInputSemantic *semantics =
                        sceGnmPsShaderInputSemanticTable(ps);
                    if (semantics && nsemantics > 0) {
                        memcpy(pipe->ps_input_semantics, semantics,
                               nsemantics * sizeof(GnmPixelInputSemantic));
                        pipe->ps_input_semantic_count = nsemantics;
                    }
                    if (ps_slots && ps_nslots > 0) {
                        memcpy(pipe->ps_input_usage_slots, ps_slots,
                               ps_nslots * sizeof(GnmInputUsageSlot));
                        pipe->ps_input_usage_slot_count = ps_nslots;
                    }
                    vk_ps4_log("pipeline: PS usage source=stage-header slots=%u generic=%u",
                               ps_nslots, nslots);
                    for (uint32_t s = 0; s < ps_nslots; s++) {
                        vk_ps4_log("pipeline: PS usage[%u] type=0x%02x api=%u start=%u raw=0x%02x",
                                   s,
                                   (unsigned)pipe->ps_input_usage_slots[s].usagetype,
                                   (unsigned)pipe->ps_input_usage_slots[s].apislot,
                                   (unsigned)pipe->ps_input_usage_slots[s].startregister,
                                   (unsigned)pipe->ps_input_usage_slots[s].srtdwordsminusone);
                    }
                    break;
                }
                case VK_SHADER_STAGE_GEOMETRY_BIT: {
                    /* GS shader binary: GnmShaderCommonData + GnmGsStageRegisters.
                     * NOTE: The current psbc compiler maps GS to GNM_SHADER_VERTEX
                     * with a GnmVsShader header. Only extract GS registers when
                     * the shader type is actually GNM_SHADER_GEOMETRY. */
                    if (metadata.type == GNM_SHADER_GEOMETRY) {
                        const uint8_t *stage_ptr = (const uint8_t *)metadata.stage;
                        const GnmGsStageRegisters *gs_regs =
                            (const GnmGsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->gs_regs = *gs_regs;
                        pipe->has_gs = true;
                        if (gpu_code) {
                            sceGnmGsRegsSetAddress(&pipe->gs_regs,
                                gpu_code);
                        }
                    }
                    /* Store the module regardless — it may be used later
                     * when the compiler properly outputs GS binaries */
                    pipe->gs_module = mod;
                    break;
                }
                case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: {
                    /* TCS (hull shader): GnmShaderCommonData + GnmHsStageRegisters.
                     * NOTE: The current psbc compiler maps TCS to GNM_SHADER_VERTEX
                     * with a GnmVsShader header. Only extract HS registers when
                     * the shader type is actually GNM_SHADER_HULL. */
                    if (metadata.type == GNM_SHADER_HULL) {
                        const uint8_t *stage_ptr = (const uint8_t *)metadata.stage;
                        const GnmHsStageRegisters *hs_regs =
                            (const GnmHsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->hs_regs = *hs_regs;
                        pipe->has_hs = true;
                        if (gpu_code) {
                            sceGnmHsRegsSetAddress(&pipe->hs_regs,
                                gpu_code);
                        }
                    }
                    pipe->tcs_module = mod;
                    break;
                }
                case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: {
                    /* TES (domain shader): compiled as VS (DS_VS) or ES (DS_ES).
                     * NOTE: The current psbc compiler maps TES to GNM_SHADER_VERTEX
                     * with a GnmVsShader header. Only extract ES registers when
                     * the shader type is GNM_SHADER_EXPORT. Otherwise, treat as VS. */
                    if (metadata.type == GNM_SHADER_EXPORT) {
                        const uint8_t *stage_ptr = (const uint8_t *)metadata.stage;
                        const GnmEsStageRegisters *es_regs =
                            (const GnmEsStageRegisters *)(stage_ptr + sizeof(GnmShaderCommonData));
                        pipe->es_regs = *es_regs;
                        pipe->has_es = true;
                        if (gpu_code) {
                            sceGnmEsRegsSetAddress(&pipe->es_regs,
                                gpu_code);
                        }
                    } else {
                        /* DS_VS or compiler fallback: treat as vertex shader.
                         * Extract VS registers so CmdBindPipeline can use them. */
                        const GnmVsShader *vs = (const GnmVsShader *)metadata.stage;
                        pipe->vs_regs = vs->registers;
                        pipe->has_ds_vs = true;
                        if (gpu_code) {
                            sceGnmVsRegsSetAddress(&pipe->vs_regs,
                                gpu_code);
                        }
                    }
                    pipe->tes_module = mod;
                    break;
                }
                default:
                    break;
                }
            }

            /* Keep the compiled binary alive for the pipeline's lifetime.
             * The stage registers contain GPU addresses that point into
             * this buffer (patched via sceGnm*RegsSetAddress above).
             * Store in the per-stage field; freed in DestroyPipeline.
             * Skip if binary == mod->binary (stub mode, no compilation). */
            if (binary && binary != mod->binary) {
                switch (stage->stage) {
                case VK_SHADER_STAGE_VERTEX_BIT:            pipe->vs_binary = binary; break;
                case VK_SHADER_STAGE_FRAGMENT_BIT:          pipe->ps_binary = binary; break;
                case VK_SHADER_STAGE_GEOMETRY_BIT:          pipe->gs_binary = binary; break;
                case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:    pipe->tcs_binary = binary; break;
                case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: pipe->tes_binary = binary; break;
                default: vk_ps4_free(alloc, binary); break;
                }
            }
        }

        /* A graphics pipeline must have at least VS with valid registers */
        if (!compile_ok || !vs_found) {
            vk_ps4_free(alloc, pipe->vertex_bindings);
            vk_ps4_free(alloc, pipe->vertex_attributes);
            vk_ps4_free(alloc, pipe->blend_attachments);
            vk_ps4_free(alloc, pipe->fetch_shader);
            if (pipe->vs_binary)  vk_ps4_free(alloc, pipe->vs_binary);
            if (pipe->ps_binary)  vk_ps4_free(alloc, pipe->ps_binary);
            if (pipe->gs_binary)  vk_ps4_free(alloc, pipe->gs_binary);
            if (pipe->tcs_binary) vk_ps4_free(alloc, pipe->tcs_binary);
            if (pipe->tes_binary) vk_ps4_free(alloc, pipe->tes_binary);
            vk_ps4_pipeline_release_shader_code(pipe);
            vk_ps4_free(alloc, pipe);
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_FEATURE_NOT_PRESENT;
            continue;
        }
        /* PS is optional but if present must have valid registers */
        if (!ps_found) {
            /* Pipeline without fragment shader — may be used for depth-only.
             * This is valid in Vulkan. PS regs stay zeroed. */
        }

        /* A fragment input must be linked to a real VS export.  Leaving
         * SPI_PS_INPUT_CNTL at reset state made pipeline creation look healthy
         * but faulted FW 5.05 inside the first native draw submit. */
        bool ps_linkage_valid = true;
        for (uint32_t p = 0; p < pipe->ps_input_semantic_count; p++) {
            uint32_t input_control;
            if (!vk_ps4_ps_input_control(pipe->vs_export_semantics,
                                        pipe->vs_export_semantic_count,
                                        &pipe->ps_input_semantics[p],
                                        &input_control)) {
                vk_ps4_log("pipeline: PS linkage REJECT input=%u semantic=%u VS-exports=%u",
                           p, (unsigned)pipe->ps_input_semantics[p].semantic,
                           pipe->vs_export_semantic_count);
                ps_linkage_valid = false;
                break;
            }
        }
        if (!ps_linkage_valid) {
            vk_ps4_free(alloc, pipe->vertex_bindings);
            vk_ps4_free(alloc, pipe->vertex_attributes);
            vk_ps4_free(alloc, pipe->blend_attachments);
            if (pipe->fetch_shader_mem.allocated)
                sceGnmDirectMemoryRelease(&pipe->fetch_shader_mem);
            else if (pipe->fetch_shader)
                vk_ps4_free(alloc, pipe->fetch_shader);
            if (pipe->vs_binary) vk_ps4_free(alloc, pipe->vs_binary);
            if (pipe->ps_binary) vk_ps4_free(alloc, pipe->ps_binary);
            vk_ps4_pipeline_release_shader_code(pipe);
            vk_ps4_free(alloc, pipe);
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_INVALID_SHADER_NV;
            continue;
        }
        vk_ps4_log("pipeline: raster linkage VS-exports=%u PS-inputs=%u validated=1 direct-spi=1",
                   pipe->vs_export_semantic_count,
                   pipe->ps_input_semantic_count);
        for (uint32_t p = 0; p < pipe->ps_input_semantic_count; ++p) {
            if (pipe->ps_input_semantics[p].semantic ==
                VK_PS4_SEMANTIC_POINT_COORD) {
                vk_ps4_log("pipeline: PS input[%u] semantic=%u <- rasterizer PointCoord sprite=1 origin=upper-left",
                           p, VK_PS4_SEMANTIC_POINT_COORD);
                continue;
            }
            for (uint32_t v = 0; v < pipe->vs_export_semantic_count; ++v) {
                if (pipe->ps_input_semantics[p].semantic ==
                    pipe->vs_export_semantics[v].semantic) {
                    vk_ps4_log("pipeline: PS input[%u] semantic=%u <- VS export[%u] outindex=%u default=%u flat=%u",
                               p,
                               (unsigned)pipe->ps_input_semantics[p].semantic,
                               v,
                               (unsigned)pipe->vs_export_semantics[v].outindex,
                               (unsigned)pipe->ps_input_semantics[p].defaultvalue,
                               (unsigned)pipe->ps_input_semantics[p].isflatshaded);
                    break;
                }
            }
        }

        /* Every PSBC descriptor-table slot now carries the original Vulkan
         * set number in apislot.  Reject corrupt/out-of-layout metadata before
         * it can bind an unrelated Garlic address. */
        VkPs4PipelineLayout *graphics_layout =
            (VkPs4PipelineLayout *)ci->layout;
        for (uint32_t slot = 0; slot < pipe->ps_input_usage_slot_count; ++slot) {
            const GnmInputUsageSlot *usage = &pipe->ps_input_usage_slots[slot];
            if (usage->usagetype == GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE &&
                (!graphics_layout || usage->apislot >=
                    graphics_layout->set_layout_count)) {
                vk_ps4_log("CreateGraphicsPipelines: descriptor set slot out of layout set=%u count=%u",
                           (unsigned)usage->apislot,
                           graphics_layout ? graphics_layout->set_layout_count : 0u);
                vk_ps4_DestroyPipeline(device, (VkPipeline)pipe, pAllocator);
                pPipelines[i] = VK_NULL_HANDLE;
                overall_result = VK_ERROR_INVALID_SHADER_NV;
                compile_ok = false;
                break;
            }
        }
        if (!compile_ok) continue;

        /* Generate fetch shader from VS input semantics + input usage slots.
         * The fetch shader tells the GPU how to load vertex attributes from
         * bound vertex buffers into VGPRs. */
        if (pipe->vs_input_semantic_count > 0 && pipe->vs_input_usage_slot_count > 0) {
            /* Find the fetch shader slot and vertex buffer table slot
             * from the input usage slot table */
            bool has_fs_slot = false;
            bool has_vb_slot = false;
            for (uint32_t s = 0; s < pipe->vs_input_usage_slot_count; s++) {
                vk_ps4_log("pipeline: VS usage[%u] type=0x%02x api=%u start=%u raw=0x%02x",
                           s,
                           (unsigned)pipe->vs_input_usage_slots[s].usagetype,
                           (unsigned)pipe->vs_input_usage_slots[s].apislot,
                           (unsigned)pipe->vs_input_usage_slots[s].startregister,
                           (unsigned)pipe->vs_input_usage_slots[s].srtdwordsminusone);
                if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER) {
                    pipe->fetch_shader_slot = pipe->vs_input_usage_slots[s].startregister;
                    has_fs_slot = true;
                } else if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE) {
                    pipe->vertex_buffer_table_slot = pipe->vs_input_usage_slots[s].startregister;
                    has_vb_slot = true;
                }
            }

            /* Both pointers are mandatory.  Generating fetch code without a
             * vertex-table SGPR makes it load descriptors through an invalid
             * address and can fault the GPU on the first draw. */
            if (!has_fs_slot || !has_vb_slot) {
                vk_ps4_log("pipeline: VS fetch ABI missing fs-slot=%u vb-slot=%u; patched PSBC generic prolog not active",
                           has_fs_slot ? 1u : 0u, has_vb_slot ? 1u : 0u);
                goto skip_fetch_shader;
            }

            /* The OpenGNM fetch generator consumes one instancing entry per
             * vertex input semantic, not one per Vulkan buffer binding.  An
             * interleaved position+normal+uv stream therefore repeats its
             * binding's rate for all three attributes. */
            GnmFetchShaderInstancingMode inst_modes[VK_PS4_MAX_INPUT_USAGE_SLOTS] = {0};
            uint32_t num_inst_modes = pipe->vs_input_semantic_count;
            bool has_instance_rate = false;
            if (ci->pVertexInputState) {
                const VkPipelineVertexInputStateCreateInfo *vi = ci->pVertexInputState;
                if (num_inst_modes > VK_PS4_MAX_INPUT_USAGE_SLOTS)
                    num_inst_modes = VK_PS4_MAX_INPUT_USAGE_SLOTS;
                for (uint32_t s = 0; s < num_inst_modes; s++) {
                    VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX;
                    const VkVertexInputAttributeDescription *attr = NULL;
                    const uint32_t semantic = pipe->vs_input_semantics[s].semantic;
                    for (uint32_t a = 0; a < vi->vertexAttributeDescriptionCount; a++) {
                        if (vi->pVertexAttributeDescriptions[a].location == semantic) {
                            attr = &vi->pVertexAttributeDescriptions[a];
                            break;
                        }
                    }
                    /* Older psbc containers can number semantics densely even
                     * when locations were not preserved.  Attribute order is
                     * the conservative fallback used by the fetch generator. */
                    if (!attr && s < vi->vertexAttributeDescriptionCount)
                        attr = &vi->pVertexAttributeDescriptions[s];
                    if (attr) {
                        for (uint32_t b = 0; b < vi->vertexBindingDescriptionCount; b++) {
                            if (vi->pVertexBindingDescriptions[b].binding == attr->binding) {
                                rate = vi->pVertexBindingDescriptions[b].inputRate;
                                break;
                            }
                        }
                    }
                    inst_modes[s] = (rate == VK_VERTEX_INPUT_RATE_INSTANCE) ?
                        GNM_FETCH_MODE_INSTANCEID : GNM_FETCH_MODE_VERTEXINDEX;
                    if (rate == VK_VERTEX_INPUT_RATE_INSTANCE)
                        has_instance_rate = true;
                }
            }

            GnmFetchShaderCreateInfo fetch_ci = {0};
            fetch_ci.regs = &pipe->vs_regs;
            fetch_ci.vtxinputs = pipe->vs_input_semantics;
            fetch_ci.numvtxinputs = pipe->vs_input_semantic_count;
            fetch_ci.inputusages = pipe->vs_input_usage_slots;
            fetch_ci.numinputusages = pipe->vs_input_usage_slot_count;
            /* OpenGNM treats a non-null instancing table as the extended
             * fetch ABI and forces VGPR_COMP_CNT=3, even when every entry is
             * VERTEXINDEX.  A normal per-vertex PSBC shader uses the native
             * VGPR_COMP_CNT=1 contract.  Only publish this table when the
             * Vulkan declaration actually contains an instance-rate input. */
            if (has_instance_rate && num_inst_modes > 0) {
                fetch_ci.instancedata = inst_modes;
                fetch_ci.numinstancedata = num_inst_modes;
            }

            vk_ps4_log("pipeline: fetch instancing=%s entries=%u",
                       has_instance_rate ? "ACTIVE" : "NONE",
                       has_instance_rate ? num_inst_modes : 0u);

            uint32_t fetch_size = 0;
            GnmError gerr = sceGnmFetchShaderCalcSize(&fetch_size, &fetch_ci);
            vk_ps4_log("pipeline: FetchShaderCalcSize result=%d bytes=%u inputs=%u",
                       (int)gerr, fetch_size, fetch_ci.numvtxinputs);
            if (gerr == GNM_ERROR_OK && fetch_size > 0) {
                /* Fetch code is executed by the GPU.  On Orbis it must be in
                 * mapped Garlic direct memory; an aligned malloc pointer is
                 * still not a valid retail GPU virtual address. */
#if defined(__ORBIS__) || defined(__PS4__)
                GnmError alloc_err = sceGnmDirectMemoryAllocate(
                    &pipe->fetch_shader_mem, fetch_size, 64u * 1024u,
                    GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
                );
                if (alloc_err == GNM_ERROR_OK)
                    pipe->fetch_shader = pipe->fetch_shader_mem.mapped;
#else
                pipe->fetch_shader = vk_ps4_alloc_zero(alloc, fetch_size, 256);
#endif
                if (pipe->fetch_shader) {
                    memset(pipe->fetch_shader, 0, fetch_size);
                    pipe->fetch_shader_size = fetch_size;
                    GnmFetchShaderResults fetch_res = {0};
                    gerr = sceGnmCreateFetchShader(
                        pipe->fetch_shader, fetch_size, &fetch_ci, &fetch_res
                    );
                    vk_ps4_log("pipeline: CreateFetchShader result=%d sgprs=%u vgprcompcnt=%u",
                               (int)gerr, fetch_res.sgprs, fetch_res.vgprcompcnt);
                    if (gerr == GNM_ERROR_OK) {
                        vk_ps4_cpu_store_fence();
                        sceGnmVsRegsSetFetchShaderModifier(&pipe->vs_regs, &fetch_res);
                        pipe->has_fetch_shader = true;
                        pipe->has_fetch_shader_slot = has_fs_slot;
                        pipe->has_vb_table_slot = has_vb_slot;
                    } else {
                        if (pipe->fetch_shader_mem.allocated)
                            sceGnmDirectMemoryRelease(&pipe->fetch_shader_mem);
                        else
                            vk_ps4_free(alloc, pipe->fetch_shader);
                        pipe->fetch_shader = NULL;
                        pipe->fetch_shader_size = 0;
                    }
                }
            }
        }
        skip_fetch_shader: ;

        vk_ps4_log("pipeline: VS vertex-fetch semantics=%u usage-slots=%u fetch=%u fetch-slot=%u vb-table=%u table-slot=%u bytes=%u",
                   pipe->vs_input_semantic_count,
                   pipe->vs_input_usage_slot_count,
                   pipe->has_fetch_shader ? 1u : 0u,
                   pipe->has_fetch_shader_slot ? 1u : 0u,
                   pipe->has_vb_table_slot ? 1u : 0u,
                   pipe->vertex_buffer_table_slot,
                   pipe->fetch_shader_size);

        /* A pipeline with declared Vulkan vertex attributes cannot draw on
         * GNM without the generic VS prolog ABI and its generated fetch
         * shader.  M6.2.3 silently accepted this state, so CmdDraw became a
         * no-op and VideoOut displayed only the clear colour.  Reject it at
         * creation time instead of publishing a non-rendering pipeline. */
        if (pipe->vertex_input_state.vertexAttributeDescriptionCount > 0 &&
            (!pipe->has_fetch_shader || !pipe->has_fetch_shader_slot ||
             !pipe->has_vb_table_slot || pipe->fetch_shader_size == 0)) {
            vk_ps4_log_raw("CreateGraphicsPipelines: FATAL vertex attributes declared but PS4 fetch ABI is incomplete");
            vk_ps4_DestroyPipeline(device, (VkPipeline)pipe, pAllocator);
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_FEATURE_NOT_PRESENT;
            continue;
        }

        /* Extract push constant inline register mapping from input usage slots.
         * psbc emits IMM_ALUFLOATCONST slots for each inlined push constant
         * dword, with apislot = push constant dword index and
         * startregister = user-data register. */
        for (uint32_t s = 0; s < pipe->vs_input_usage_slot_count; s++) {
            if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST) {
                uint8_t apislot = pipe->vs_input_usage_slots[s].apislot;
                if (apislot == 0xFE) {
                    /* base_vertex (vertexOffset) */
                    pipe->vs_base_vertex_reg = pipe->vs_input_usage_slots[s].startregister;
                    pipe->has_base_vertex_reg = true;
                } else if (apislot == 0xFF) {
                    /* start_instance (firstInstance) */
                    pipe->vs_start_instance_reg = pipe->vs_input_usage_slots[s].startregister;
                    pipe->has_start_instance_reg = true;
                } else if (pipe->vs_push_const_slot_count < VK_PS4_MAX_PUSH_CONST_DWORDS) {
                    /* Regular push constant dword */
                    pipe->vs_push_const_slots[pipe->vs_push_const_slot_count].dword_index = apislot;
                    pipe->vs_push_const_slots[pipe->vs_push_const_slot_count].user_data_reg =
                        pipe->vs_input_usage_slots[s].startregister;
                    pipe->vs_push_const_slot_count++;
                }
            }
        }
        for (uint32_t s = 0; s < pipe->ps_input_usage_slot_count; s++) {
            if (pipe->ps_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST) {
                uint8_t apislot = pipe->ps_input_usage_slots[s].apislot;
                /* Skip 0xFE/0xFF — these are VS-only special slots
                 * for base_vertex/start_instance. They should never
                 * appear in PS, but filter defensively. */
                if (apislot >= 0xFE) continue;
                if (pipe->ps_push_const_slot_count < VK_PS4_MAX_PUSH_CONST_DWORDS) {
                    pipe->ps_push_const_slots[pipe->ps_push_const_slot_count].dword_index = apislot;
                    pipe->ps_push_const_slots[pipe->ps_push_const_slot_count].user_data_reg =
                        pipe->ps_input_usage_slots[s].startregister;
                    pipe->ps_push_const_slot_count++;
                }
            }
        }

        pPipelines[i] = (VkPipeline)pipe;
    }

    return overall_result;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                               uint32_t createInfoCount,
                               const VkComputePipelineCreateInfo *pCreateInfos,
                               const VkAllocationCallbacks *pAllocator,
                               VkPipeline *pPipelines) {

    if (!device || !pCreateInfos || !pPipelines) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    VkResult overall_result = VK_SUCCESS;

    for (uint32_t i = 0; i < createInfoCount; i++) {
        const VkComputePipelineCreateInfo *ci = &pCreateInfos[i];
        VkPs4Pipeline *pipe = vk_ps4_alloc_zero(alloc, sizeof(*pipe), 16);
        if (!pipe) {
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_OUT_OF_HOST_MEMORY;
            continue;
        }
        pipe->type = VK_PS4_OBJ_PIPELINE;
        pipe->device = dev;
        pipe->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
        memset(&pipe->cs_regs, 0, sizeof(pipe->cs_regs));

        VkPs4ShaderModule *mod = (VkPs4ShaderModule *)ci->stage.module;
        void *binary = NULL;
        size_t binary_size = 0;
        GnmShaderMetadata metadata = {0};

        /* Check pipeline cache first */
        uint64_t cache_hash = 0;
        size_t cached_size = 0;
        void *cached_binary = NULL;
        if (pipelineCache && mod->binary && mod->binary_size > 0) {
            cache_hash = vk_ps4_pipeline_cache_hash(mod->binary, mod->binary_size,
                                                    (uint32_t)ci->stage.stage);
            cache_hash = vk_ps4_pipeline_layout_hash(
                cache_hash, (const VkPs4PipelineLayout *)ci->layout);
            cached_binary = vk_ps4_pipeline_cache_lookup(pipelineCache, cache_hash,
                                                         (uint32_t)ci->stage.stage,
                                                         &cached_size);
        }

        if (cached_binary && cached_size > 0) {
            binary = vk_ps4_alloc(alloc, cached_size, 16);
            if (binary) {
                memcpy(binary, cached_binary, cached_size);
                binary_size = cached_size;
                sceGnmShaderBinaryGetMetadata(binary, binary_size, &metadata);
            }
        } else {
            VkResult vr = vk_ps4_compile_shader_module(
                mod, ci->stage.stage, (const VkPs4PipelineLayout *)ci->layout,
                alloc, &binary, &binary_size, &metadata
            );
            if (vr != VK_SUCCESS) {
                if (binary && binary != mod->binary) {
                    vk_ps4_free(alloc, binary);
                }
                vk_ps4_free(alloc, pipe);
                pPipelines[i] = VK_NULL_HANDLE;
                overall_result = VK_ERROR_FEATURE_NOT_PRESENT;
                continue;
            }
            if (pipelineCache && binary && binary_size > 0 && mod->binary && mod->binary_size > 0) {
                vk_ps4_pipeline_cache_insert(pipelineCache, cache_hash,
                                             (uint32_t)ci->stage.stage,
                                             (uint32_t)mod->binary_size,
                                             binary, binary_size);
            }
        }

        bool cs_ok = false;
        VkPs4ComputeShader decoded;
        const char *decode_error = vk_ps4_decode_compute_shader(
            binary, binary_size, &metadata, VK_PS4_MAX_INPUT_USAGE_SLOTS, &decoded);
        if (!decode_error) {
            pipe->cs_module = mod;
            pipe->cs_regs = decoded.registers;
            memcpy(pipe->vs_input_usage_slots, decoded.slots,
                   decoded.slot_count * sizeof(GnmInputUsageSlot));
            pipe->vs_input_usage_slot_count = decoded.slot_count;
            void *gpu_code = NULL;
            const VkResult upload_result = vk_ps4_pipeline_upload_shader_code(
                pipe, VK_SHADER_STAGE_COMPUTE_BIT, decoded.code,
                decoded.code_size, NULL, &gpu_code);
            if (upload_result == VK_SUCCESS) {
                sceGnmCsRegsSetAddress(&pipe->cs_regs, gpu_code);
                cs_ok = true;
                vk_ps4_log("pipeline: CS decoded threads=%ux%ux%u rsrc1=0x%x rsrc2=0x%x slots=%u code=%u",
                    pipe->cs_regs.computenumthreadx, pipe->cs_regs.computenumthready,
                    pipe->cs_regs.computenumthreadz, pipe->cs_regs.computepgmrsrc1,
                    pipe->cs_regs.computepgmrsrc2, decoded.slot_count, decoded.code_size);
                for (uint32_t slot = 0; slot < decoded.slot_count; ++slot) {
                    const GnmInputUsageSlot *usage = &pipe->vs_input_usage_slots[slot];
                    vk_ps4_log("pipeline: CS usage[%u] type=0x%x api=%u start=%u",
                        slot, usage->usagetype, usage->apislot, usage->startregister);
                }
            }

            /* Extract push constant inline register mapping for CS.
             * Filter out 0xFE/0xFF special slots (VS-only base_vertex/
             * start_instance) defensively — they should never appear
             * in a CS shader. */
            for (uint32_t s = 0; s < pipe->vs_input_usage_slot_count; s++) {
                if (pipe->vs_input_usage_slots[s].usagetype == GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST) {
                    uint8_t apislot = pipe->vs_input_usage_slots[s].apislot;
                    if (apislot >= 0xFE) continue;
                    if (pipe->cs_push_const_slot_count < VK_PS4_MAX_PUSH_CONST_DWORDS) {
                        pipe->cs_push_const_slots[pipe->cs_push_const_slot_count].dword_index = apislot;
                        pipe->cs_push_const_slots[pipe->cs_push_const_slot_count].user_data_reg =
                            pipe->vs_input_usage_slots[s].startregister;
                        pipe->cs_push_const_slot_count++;
                    }
                }
            }

        }

        if (decode_error) vk_ps4_log("pipeline: CS REJECT %s", decode_error);

        /* Retain the compiled container for pipeline lifetime. Executable code
         * has a separate aligned GPU allocation. Freed in DestroyPipeline. */
        if (binary && binary != mod->binary) {
            pipe->cs_binary = binary;
        }

        if (!cs_ok) {
            if (pipe->cs_binary) vk_ps4_free(alloc, pipe->cs_binary);
            vk_ps4_pipeline_release_shader_code(pipe);
            vk_ps4_free(alloc, pipe);
            pPipelines[i] = VK_NULL_HANDLE;
            overall_result = VK_ERROR_FEATURE_NOT_PRESENT;
            continue;
        }

        pPipelines[i] = (VkPipeline)pipe;
    }

    return overall_result;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator) {
    if (!device || !pipeline) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4Pipeline *pipe = (VkPs4Pipeline *)pipeline;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    if (pipe->fetch_shader_mem.allocated)
        sceGnmDirectMemoryRelease(&pipe->fetch_shader_mem);
    else if (pipe->fetch_shader)
        vk_ps4_free(alloc, pipe->fetch_shader);
    if (pipe->vertex_bindings) {
        vk_ps4_free(alloc, pipe->vertex_bindings);
    }
    if (pipe->vertex_attributes) {
        vk_ps4_free(alloc, pipe->vertex_attributes);
    }
    if (pipe->blend_attachments) {
        vk_ps4_free(alloc, pipe->blend_attachments);
    }
    /* Release aligned executable allocations before their metadata
     * containers. */
    vk_ps4_pipeline_release_shader_code(pipe);
    /* Free compiled GCN shader containers retained for metadata tables. */
    if (pipe->vs_binary)  vk_ps4_free(alloc, pipe->vs_binary);
    if (pipe->ps_binary)  vk_ps4_free(alloc, pipe->ps_binary);
    if (pipe->gs_binary)  vk_ps4_free(alloc, pipe->gs_binary);
    if (pipe->tcs_binary) vk_ps4_free(alloc, pipe->tcs_binary);
    if (pipe->tes_binary) vk_ps4_free(alloc, pipe->tes_binary);
    if (pipe->cs_binary)  vk_ps4_free(alloc, pipe->cs_binary);
    vk_ps4_free(alloc, pipe);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout) {
    if (!device || !pCreateInfo || !pPipelineLayout) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pCreateInfo->setLayoutCount > VK_PS4_MAX_DESCRIPTOR_SETS)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; ++set)
        if (!pCreateInfo->pSetLayouts[set])
            return VK_ERROR_INITIALIZATION_FAILED;

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4PipelineLayout *layout = vk_ps4_alloc_zero(alloc, sizeof(*layout), 16);
    if (!layout) return VK_ERROR_OUT_OF_HOST_MEMORY;
    layout->type = VK_PS4_OBJ_PIPELINE_LAYOUT;
    layout->device = dev;
    layout->create_info = *pCreateInfo;
    layout->set_layout_count = pCreateInfo->setLayoutCount;
    layout->push_constant_range_count = pCreateInfo->pushConstantRangeCount;

    /* Copy set layout pointers */
    if (layout->set_layout_count > 0) {
        layout->set_layouts = vk_ps4_alloc_zero(alloc,
            layout->set_layout_count * sizeof(VkPs4DescriptorSetLayout *), 16);
        if (!layout->set_layouts) {
            vk_ps4_free(alloc, layout);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        for (uint32_t i = 0; i < layout->set_layout_count; i++) {
            layout->set_layouts[i] = (VkPs4DescriptorSetLayout *)pCreateInfo->pSetLayouts[i];
        }
    }

    /* Copy push constant ranges */
    if (layout->push_constant_range_count > 0) {
        layout->push_constant_ranges = vk_ps4_alloc_zero(alloc,
            layout->push_constant_range_count * sizeof(VkPushConstantRange), 16);
        if (!layout->push_constant_ranges) {
            vk_ps4_free(alloc, layout->set_layouts);
            vk_ps4_free(alloc, layout);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memcpy(layout->push_constant_ranges, pCreateInfo->pPushConstantRanges,
               layout->push_constant_range_count * sizeof(VkPushConstantRange));
    }

    *pPipelineLayout = (VkPipelineLayout)layout;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator) {
    if (!device || !pipelineLayout) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4PipelineLayout *layout = (VkPs4PipelineLayout *)pipelineLayout;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, layout->set_layouts);
    vk_ps4_free(alloc, layout->push_constant_ranges);
    vk_ps4_free(alloc, layout);
}

/* === Descriptor set layout === */

static uint32_t vk_ps4_descriptor_layout_stride(VkDescriptorType type) {
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

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout) {
    if (!device || !pCreateInfo || !pSetLayout) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pCreateInfo->bindingCount > VK_PS4_MAX_DESCRIPTOR_BINDINGS)
        return VK_ERROR_FEATURE_NOT_PRESENT;

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4DescriptorSetLayout *layout = vk_ps4_alloc_zero(alloc, sizeof(*layout), 16);
    if (!layout) return VK_ERROR_OUT_OF_HOST_MEMORY;
    layout->type = VK_PS4_OBJ_DESCRIPTOR_SET_LAYOUT;
    layout->device = dev;
    layout->create_info = *pCreateInfo;
    layout->binding_count = pCreateInfo->bindingCount;
    layout->variable_descriptor_binding = UINT32_MAX;

    if (layout->binding_count > 0) {
        layout->bindings = vk_ps4_alloc_zero(alloc,
            layout->binding_count * sizeof(VkDescriptorSetLayoutBinding), 16);
        if (!layout->bindings) {
            vk_ps4_free(alloc, layout);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        memcpy(layout->bindings, pCreateInfo->pBindings,
               layout->binding_count * sizeof(VkDescriptorSetLayoutBinding));
        for (uint32_t i = 0; i < layout->binding_count; i++) {
            /* Never silently discard immutable samplers: a dangling pointer
             * would produce a layout that differs from the shader ABI. */
            if (layout->bindings[i].pImmutableSamplers) {
                vk_ps4_free(alloc, layout->bindings);
                vk_ps4_free(alloc, layout);
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            layout->bindings[i].pImmutableSamplers = NULL;
            const uint32_t stride = vk_ps4_descriptor_layout_stride(
                layout->bindings[i].descriptorType);
            if (stride == 0 || layout->bindings[i].descriptorCount == 0) {
                vk_ps4_free(alloc, layout->bindings);
                vk_ps4_free(alloc, layout);
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            if (layout->table_size > UINT32_MAX - 15u) {
                vk_ps4_free(alloc, layout->bindings);
                vk_ps4_free(alloc, layout);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            layout->table_size = (layout->table_size + 15u) & ~15u;
            layout->binding_offsets[i] = layout->table_size;
            layout->binding_strides[i] = stride;
            const uint64_t binding_size = (uint64_t)stride *
                layout->bindings[i].descriptorCount;
            if (binding_size > UINT32_MAX - layout->table_size) {
                vk_ps4_free(alloc, layout->bindings);
                vk_ps4_free(alloc, layout);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            layout->table_size += (uint32_t)binding_size;
            if (layout->bindings[i].descriptorType ==
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                layout->bindings[i].descriptorType ==
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
                layout->binding_dynamic_offsets[i] =
                    layout->dynamic_descriptor_count;
                if (layout->bindings[i].descriptorCount >
                    VK_PS4_MAX_DYNAMIC_DESCRIPTORS_PER_SET -
                        layout->dynamic_descriptor_count) {
                    vk_ps4_free(alloc, layout->bindings);
                    vk_ps4_free(alloc, layout);
                    return VK_ERROR_FEATURE_NOT_PRESENT;
                }
                layout->dynamic_descriptor_count +=
                    layout->bindings[i].descriptorCount;
            }
        }
        if (layout->table_size > UINT32_MAX - 15u) {
            vk_ps4_free(alloc, layout->bindings);
            vk_ps4_free(alloc, layout);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        layout->table_size = (layout->table_size + 15u) & ~15u;
        /* Wire deep-copied bindings into create_info */
        layout->create_info.pBindings = layout->bindings;
    }

    /* VK_EXT_descriptor_indexing: extract binding flags from pNext chain */
    VkBaseInStructure *chain = (VkBaseInStructure *)pCreateInfo->pNext;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT) {
            VkDescriptorSetLayoutBindingFlagsCreateInfo *flags_info =
                (VkDescriptorSetLayoutBindingFlagsCreateInfo *)chain;
            if (flags_info->bindingCount > 0 && flags_info->pBindingFlags) {
                uint32_t fcount = flags_info->bindingCount;
                if (fcount > layout->binding_count) fcount = layout->binding_count;
                layout->binding_flags = vk_ps4_alloc_zero(alloc,
                    layout->binding_count * sizeof(VkDescriptorBindingFlags), 16);
                if (!layout->binding_flags) {
                    vk_ps4_free(alloc, layout->bindings);
                    vk_ps4_free(alloc, layout);
                    return VK_ERROR_OUT_OF_HOST_MEMORY;
                }
                memcpy(layout->binding_flags, flags_info->pBindingFlags,
                       fcount * sizeof(VkDescriptorBindingFlags));
                /* Find the variable-count binding (must be the last one
                 * with VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT) */
                for (uint32_t i = 0; i < fcount; i++) {
                    if (flags_info->pBindingFlags[i] &
                        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT) {
                        layout->variable_descriptor_binding = i;
                    }
                }
            }
            break;
        }
        chain = (VkBaseInStructure *)chain->pNext;
    }

    *pSetLayout = (VkDescriptorSetLayout)layout;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout setLayout, const VkAllocationCallbacks *pAllocator) {
    if (!device || !setLayout) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4DescriptorSetLayout *layout = (VkPs4DescriptorSetLayout *)setLayout;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    vk_ps4_free(alloc, layout->binding_flags);
    vk_ps4_free(alloc, layout->bindings);
    vk_ps4_free(alloc, layout);
}

/* === Descriptor pool / set (Phase 2 — minimal stubs for Phase 1) === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool) {
    if (!device || !pCreateInfo || !pDescriptorPool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    /* PS4 is a 64-bit target; every uint32_t maxSets value is representable
     * in this size_t calculation.  Impractically large requests fail through
     * the allocator without truncating the requested byte count. */
    const size_t tracking_bytes =
        (size_t)pCreateInfo->maxSets * 2u * sizeof(VkPs4DescriptorSet *);
    VkPs4DescriptorPool *pool = vk_ps4_alloc_zero(
        alloc, sizeof(*pool) + tracking_bytes, 16);
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = VK_PS4_OBJ_DESCRIPTOR_POOL;
    pool->device = dev;
    pool->create_info = *pCreateInfo;
    *pDescriptorPool = (VkDescriptorPool)pool;
    return VK_SUCCESS;
}

static void vk_ps4_destroy_descriptor_set_storage(
    VkPs4DescriptorSet *set, const VkAllocationCallbacks *alloc
);

/* === Descriptor pool serialization ===
 *
 * Every pool mutator below moves a shared cursor - active_count, free_count,
 * sets_allocated - and the allocating one also calls sceGnmDirectMemoryAllocate
 * for the set's Garlic descriptor table. None of that was guarded, and nothing
 * else covered it: vk_ps4_queue.c's submit lock was the only lock of any kind
 * in this ICD, which is what "not internally thread-safe" meant.
 *
 * Vulkan makes the application responsible for this - a VkDescriptorPool is
 * externally synchronized, so a client that allocates from one pool on two
 * threads is the one at fault. This one no longer does: renderM2Particles used
 * to allocate while recording, and on a platform that records that secondary
 * on a worker that was exactly the case, but it does not any more, and the
 * pre-allocation it now relies on runs on the main thread. So the lock is
 * uncontended in the shipping path and costs one atomic per set.
 *
 * It is here so that getting that wrong later fails as contention rather than
 * as two threads swapping entries in one free list - which corrupts the pool
 * quietly and is eventually observed as a draw bound to a descriptor set that
 * was freed underneath it.
 *
 * A test-and-set lock rather than a pthread mutex, for the reason the submit
 * lock gives: this ICD builds with no dependencies. It is never taken
 * recursively - none of the holders below calls another - and never held
 * across a submit, so it cannot order against the queue lock. */
static volatile int g_vk_ps4_descriptor_lock;

static void vk_ps4_descriptor_lock(void) {
    while (__atomic_test_and_set(&g_vk_ps4_descriptor_lock, __ATOMIC_ACQUIRE)) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_M_X64))
        /* PAUSE, not a syscall. These critical sections are a handful of
         * pointer moves plus one direct-memory allocation, so a waiter is far
         * better off spinning than paying a PS4 scheduler round trip - the
         * same reasoning as the queue lock's spin tier, without the sleep tier
         * it needs for waits that can last a whole GPU batch. */
        __builtin_ia32_pause();
#endif
    }
}

static void vk_ps4_descriptor_unlock(void) {
    __atomic_clear(&g_vk_ps4_descriptor_lock, __ATOMIC_RELEASE);
}

/* The public object structure retains its original 256-entry arrays for ABI
 * compatibility.  New pools carry two maxSets-sized arrays directly after the
 * object so their Vulkan-declared capacity, rather than that legacy constant,
 * controls tracking. */
static VkPs4DescriptorSet **vk_ps4_descriptor_pool_active(
    VkPs4DescriptorPool *pool
) {
    return (VkPs4DescriptorSet **)(pool + 1);
}

static VkPs4DescriptorSet **vk_ps4_descriptor_pool_free(
    VkPs4DescriptorPool *pool
) {
    return vk_ps4_descriptor_pool_active(pool) + pool->create_info.maxSets;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator) {
    if (!device || !descriptorPool) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4DescriptorPool *pool = (VkPs4DescriptorPool *)descriptorPool;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    /* Held over the teardown so a concurrent Allocate/Free cannot be walking
     * the arrays this frees. It does not make destroying a pool that is still
     * in use legal - nothing can - but it keeps the pool's own bookkeeping
     * consistent right up to the free. */
    vk_ps4_descriptor_lock();
    /* Vulkan implicitly frees sets that are still active when their pool is
     * destroyed.  This is the normal M6.2 shutdown path. */
    for (uint32_t i = 0; i < pool->active_count; i++) {
        vk_ps4_destroy_descriptor_set_storage(
            vk_ps4_descriptor_pool_active(pool)[i], alloc);
        vk_ps4_descriptor_pool_active(pool)[i] = NULL;
    }
    pool->active_count = 0;
    /* Free all descriptor sets in the free list */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        VkPs4DescriptorSet *free_set = vk_ps4_descriptor_pool_free(pool)[i];
        if (free_set) {
            vk_ps4_release_descriptor_set_resources(free_set);
            vk_ps4_free(alloc, free_set);
        }
    }
    pool->free_count = 0;
    vk_ps4_free(alloc, pool);
    vk_ps4_descriptor_unlock();
}

static void vk_ps4_destroy_descriptor_set_storage(
    VkPs4DescriptorSet *set, const VkAllocationCallbacks *alloc
) {
    if (!set) return;
    vk_ps4_release_descriptor_set_resources(set);
    vk_ps4_free(alloc, set);
}

static bool vk_ps4_descriptor_pool_track(
    VkPs4DescriptorPool *pool, VkPs4DescriptorSet *set
) {
    if (!pool) return true;
    if (!set || pool->active_count >= pool->create_info.maxSets) return false;
    vk_ps4_descriptor_pool_active(pool)[pool->active_count++] = set;
    return true;
}

static bool vk_ps4_descriptor_pool_untrack(
    VkPs4DescriptorPool *pool, VkPs4DescriptorSet *set
) {
    if (!pool) return true;
    for (uint32_t i = 0; i < pool->active_count; i++) {
        if (vk_ps4_descriptor_pool_active(pool)[i] == set) {
            pool->active_count--;
            vk_ps4_descriptor_pool_active(pool)[i] =
                vk_ps4_descriptor_pool_active(pool)[pool->active_count];
            vk_ps4_descriptor_pool_active(pool)[pool->active_count] = NULL;
            return true;
        }
    }
    return false;
}

/* The body runs with the descriptor lock held; the entry point below takes it.
 * Split rather than wrapped in place because this returns from a dozen places
 * and one of them forgetting to unlock is a hang, not a leak. */
static VkResult vk_ps4_allocate_descriptor_sets_locked(
    VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
    VkDescriptorSet *pDescriptorSets) {
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4DescriptorPool *pool = (VkPs4DescriptorPool *)pAllocateInfo->descriptorPool;
    if (pool && (pool->active_count > pool->create_info.maxSets ||
        pAllocateInfo->descriptorSetCount >
            pool->create_info.maxSets - pool->active_count)) {
        /* Said out loud: a client that ignores this result binds a null set
         * and draws nothing, and the log is the only place it shows. */
        static unsigned exhausted_count = 0;
        if (exhausted_count++ % 50u == 0u)
            vk_ps4_log("AllocateDescriptorSets: pool exhausted active=%u max=%u requested=%u (occurrence %u)",
                       (unsigned)pool->active_count, (unsigned)pool->create_info.maxSets,
                       (unsigned)pAllocateInfo->descriptorSetCount, exhausted_count);
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    }

    /* VK_EXT_descriptor_indexing: extract variable descriptor counts from pNext */
    const uint32_t *var_counts = NULL;
    uint32_t var_count_n = 0;
    VkBaseInStructure *chain = (VkBaseInStructure *)pAllocateInfo->pNext;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT) {
            VkDescriptorSetVariableDescriptorCountAllocateInfo *vc =
                (VkDescriptorSetVariableDescriptorCountAllocateInfo *)chain;
            var_counts = vc->pDescriptorCounts;
            var_count_n = vc->descriptorSetCount;
            break;
        }
        chain = (VkBaseInStructure *)chain->pNext;
    }

    /* Validate the whole request before taking any pool entries. */
    if (pAllocateInfo->descriptorSetCount && !pAllocateInfo->pSetLayouts)
        return VK_ERROR_INITIALIZATION_FAILED;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i)
        pDescriptorSets[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        const VkPs4DescriptorSetLayout *layout =
            (const VkPs4DescriptorSetLayout *)pAllocateInfo->pSetLayouts[i];
        if (!layout || layout->binding_count > VK_PS4_MAX_DESCRIPTOR_BINDINGS)
            return VK_ERROR_INITIALIZATION_FAILED;
    }

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
        VkPs4DescriptorSet *set = NULL;
        const VkPs4DescriptorSetLayout *requested_layout =
            (const VkPs4DescriptorSetLayout *)pAllocateInfo->pSetLayouts[i];
        const uint32_t required_bindings = requested_layout->binding_count;

        /* Try to reuse from the pool's free list first */
        if (pool && pool->free_count > 0) {
            set = vk_ps4_descriptor_pool_free(pool)[--pool->free_count];
            vk_ps4_descriptor_pool_free(pool)[pool->free_count] = NULL;
            if (set->binding_capacity < required_bindings) {
                vk_ps4_destroy_descriptor_set_storage(set, alloc);
                set = NULL;
            } else {
                const uint32_t capacity = set->binding_capacity;
                memset(set, 0, sizeof(*set) + (size_t)capacity * sizeof(*set->bindings));
                set->binding_capacity = capacity;
                set->bindings = (VkPs4DescriptorBinding *)(set + 1);
            }
        }

        if (!set) {
            const size_t host_bytes = sizeof(*set) +
                (size_t)required_bindings * sizeof(VkPs4DescriptorBinding);
            set = vk_ps4_alloc_zero(alloc, host_bytes, 16);
            if (!set) {
                vk_ps4_log("AllocateDescriptorSets: HOST allocation FAILED bytes=%zu bindings=%u set=%u",
                           host_bytes, required_bindings, i);
                for (uint32_t j = 0; j < i; j++) {
                    if (pool) vk_ps4_descriptor_pool_untrack(
                        pool, (VkPs4DescriptorSet *)pDescriptorSets[j]);
                    vk_ps4_destroy_descriptor_set_storage(
                        (VkPs4DescriptorSet *)pDescriptorSets[j], alloc);
                    pDescriptorSets[j] = VK_NULL_HANDLE;
                    if (pool && pool->sets_allocated > 0)
                        pool->sets_allocated--;
                }
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            set->binding_capacity = required_bindings;
            set->bindings = (VkPs4DescriptorBinding *)(set + 1);
        }

        set->type = VK_PS4_OBJ_DESCRIPTOR_SET;
        set->device = dev;
        set->pool = pool;
        set->layout = (VkPs4DescriptorSetLayout *)pAllocateInfo->pSetLayouts[i];
        set->variable_descriptor_count = 0;

        /* Initialize bindings from the layout */
        if (set->layout && set->layout->binding_count <= VK_PS4_MAX_DESCRIPTOR_BINDINGS) {
            set->binding_count = set->layout->binding_count;
            for (uint32_t b = 0; b < set->binding_count; b++) {
                set->bindings[b].type = set->layout->bindings[b].descriptorType;
                set->bindings[b].count = set->layout->bindings[b].descriptorCount;
                set->bindings[b].binding_number = set->layout->bindings[b].binding;
                set->bindings[b].table_offset = set->layout->binding_offsets[b];
                set->bindings[b].descriptor_stride = set->layout->binding_strides[b];
                set->bindings[b].dynamic_offset_offset =
                    set->layout->binding_dynamic_offsets[b];
                set->bindings[b].resources_allocated = false;
                memset(&set->bindings[b].resource_mem, 0,
                       sizeof(set->bindings[b].resource_mem));
                set->bindings[b].buffers = NULL;
                set->bindings[b].textures = NULL;
                set->bindings[b].samplers = NULL;
                set->bindings[b].table_base = NULL;
                /* Resource arrays are allocated lazily in UpdateDescriptorSets */
            }

            /* VK_EXT_descriptor_indexing: override the variable-count binding's
             * descriptor count if provided in the allocate info pNext chain. */
            if (set->layout->variable_descriptor_binding != UINT32_MAX &&
                var_counts && i < var_count_n) {
                uint32_t vb = set->layout->variable_descriptor_binding;
                set->bindings[vb].count = var_counts[i];
                set->variable_descriptor_count = var_counts[i];
            }
        }

        /* Allocate every potential PSBC PTR_*TABLE before this VkResult API
         * succeeds.  vkUpdateDescriptorSets is void and cannot report a
         * Garlic OOM; lazy allocation there could otherwise turn a successful
         * set allocation into a silently unbindable/null GPU table. */
        VkResult table_result =
            vk_ps4_allocate_descriptor_set_resources(set, alloc);
        if (table_result == VK_SUCCESS &&
            !vk_ps4_descriptor_pool_track(pool, set)) {
            table_result = VK_ERROR_OUT_OF_POOL_MEMORY;
        }
        if (table_result != VK_SUCCESS) {
            vk_ps4_log("AllocateDescriptorSets: Garlic table allocation FAILED set=%u rc=%d",
                       i, (int)table_result);
            vk_ps4_destroy_descriptor_set_storage(set, alloc);
            for (uint32_t j = 0; j < i; j++) {
                if (pool) vk_ps4_descriptor_pool_untrack(
                    pool, (VkPs4DescriptorSet *)pDescriptorSets[j]);
                vk_ps4_destroy_descriptor_set_storage(
                    (VkPs4DescriptorSet *)pDescriptorSets[j], alloc);
                pDescriptorSets[j] = VK_NULL_HANDLE;
                if (pool && pool->sets_allocated > 0)
                    pool->sets_allocated--;
            }
            return table_result;
        }

        if (pool) pool->sets_allocated++;
        pDescriptorSets[i] = (VkDescriptorSet)set;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                              VkDescriptorSet *pDescriptorSets) {
    if (!device || !pAllocateInfo || !pDescriptorSets) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    vk_ps4_descriptor_lock();
    const VkResult result = vk_ps4_allocate_descriptor_sets_locked(
        device, pAllocateInfo, pDescriptorSets);
    vk_ps4_descriptor_unlock();
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                          uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets) {
    if (!device || !pDescriptorSets) return VK_SUCCESS;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4DescriptorPool *pool = (VkPs4DescriptorPool *)descriptorPool;
    vk_ps4_descriptor_lock();
    for (uint32_t i = 0; i < descriptorSetCount; i++) {
        if (!pDescriptorSets[i]) continue;
        VkPs4DescriptorSet *set = (VkPs4DescriptorSet *)pDescriptorSets[i];
        if (pool && !vk_ps4_descriptor_pool_untrack(pool, set)) {
            /* Already freed, or belongs to a different pool. */
            continue;
        }
        vk_ps4_release_descriptor_set_resources(set);
        /* Add to the pool's free list for reuse, or free if pool is full */
        if (pool && pool->free_count < pool->create_info.maxSets) {
            vk_ps4_descriptor_pool_free(pool)[pool->free_count++] = set;
        } else {
            vk_ps4_free(alloc, set);
        }
        if (pool) pool->sets_freed++;
    }
    vk_ps4_descriptor_unlock();
    return VK_SUCCESS;
}

/* UpdateDescriptorSets moved to vk_ps4_descriptor.c */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorPoolResetFlags flags) {
    (void)flags;
    if (!device || !descriptorPool) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4DescriptorPool *pool = (VkPs4DescriptorPool *)descriptorPool;
    vk_ps4_descriptor_lock();

    /* Reset implicitly frees all currently active sets and their Garlic
     * descriptor tables. */
    for (uint32_t i = 0; i < pool->active_count; i++) {
        vk_ps4_destroy_descriptor_set_storage(
            vk_ps4_descriptor_pool_active(pool)[i], alloc);
        vk_ps4_descriptor_pool_active(pool)[i] = NULL;
    }
    pool->active_count = 0;

    /* Free all descriptor sets in the free list */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        VkPs4DescriptorSet *free_set = vk_ps4_descriptor_pool_free(pool)[i];
        if (free_set) {
            vk_ps4_release_descriptor_set_resources(free_set);
            vk_ps4_free(alloc, free_set);
            vk_ps4_descriptor_pool_free(pool)[i] = NULL;
        }
    }
    pool->free_count = 0;
    pool->sets_allocated = 0;
    pool->sets_freed = 0;
    vk_ps4_descriptor_unlock();
    return VK_SUCCESS;
}

/* === Sampler === */
/* (Moved to vk_ps4_descriptor.c for proper GnmSampler initialization) */
