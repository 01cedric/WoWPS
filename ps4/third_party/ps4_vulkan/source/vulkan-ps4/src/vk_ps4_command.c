/*
 * vk_ps4_command.c — VkCommandBuffer implementation via GnmCommandBuffer.
 *
 * vkBeginCommandBuffer allocates PM4 buffer and inits default hardware state.
 * vkCmdBindPipeline emits SetVsShader/SetPsShader + state registers.
 * vkCmdSetViewport / SetScissor emit corresponding PM4 packets.
 * vkCmdDraw emits DrawIndexAuto.
 * vkCmdBeginRenderPass emits SetRenderTarget + clear.
 * vkCmdEndRenderPass emits EOP event.
 * vkEndCommandBuffer finalizes the PM4 stream.
 */

#include "vk_ps4_internal.h"
#include "vk_ps4_shader_linkage.h"
#include <gpuaddr.h>
#include "vk_ps4_subresource.h"
#include "vk_ps4_dispatch.h"

#include <string.h>
#include <math.h>

/* PM4 register definitions for direct register programming
 * (stencil ref/mask/ops don't have GNM API wrappers). */
#include <pm4/sid.h>
#include <pm4/amdgfxregs.h>

/* Forward declaration from vk_ps4_pipeline.c */
extern GnmPrimitiveType vk_topology_to_gnm(VkPrimitiveTopology topology);

/* Forward declarations for format helpers defined later in this file */
static uint32_t vk_format_to_bpp(VkFormat fmt);

/* Vulkan recording commands return void. Preserve the first failure until
 * EndCommandBuffer / QueueSubmit so unsupported transfers cannot look like
 * successful frames with black or stale textures. */
static void vk_ps4_command_fail(VkPs4CommandBuffer *cmd, const char *reason) {
    if (!cmd) return;
    if (cmd->recording_error == VK_SUCCESS) {
        vk_ps4_log("B3 recording rejected: %s", reason);
        cmd->recording_error = VK_ERROR_FEATURE_NOT_PRESENT;
    }
}

/* Grow by appending a new mapped DCB, never by moving an old one: inline
 * constants already contain its GPU addresses. All segments execute in order
 * in one native submit; graphics state and immutable descriptor arenas survive
 * the boundary. The cap is deliberate, and failure still rejects the frame. */
static bool vk_ps4_command_overflow(GnmCommandBuffer *gnm, uint32_t requested,
                                   void *userdata) {
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)userdata;
    if (!cmd || !gnm || cmd->recording_error != VK_SUCCESS) return false;
    const uint32_t count = cmd->pm4_segment_count;
    if (count && count < VK_PS4_MAX_PM4_SEGMENTS && requested <= cmd->pm4_buffer_size &&
        cmd->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
        GnmDirectMemory *next = &cmd->pm4_extra[count - 1u];
        if (!next->allocated) {
            if (sceGnmDirectMemoryAllocate(next, VK_PS4_CMD_BUFFER_ALLOCATION_SIZE,
                    64u * 1024u, GNM_DIRECT_MEMORY_TYPE_WC_GARLIC,
                    GNM_PROT_CPU_GPU_RW) != GNM_ERROR_OK) goto exhausted;
            vk_ps4_log("[PM4_SEGMENTS] allocated segment=%u bytes=%u cap=%u",
                       count + 1u, VK_PS4_CMD_BUFFER_ALLOCATION_SIZE, VK_PS4_MAX_PM4_SEGMENTS);
        }
        if (!next->mapped) goto exhausted;
        cmd->pm4_segment_used[count - 1u] = (uint32_t)(gnm->cmdptr - gnm->beginptr);
        const GnmCommandBufferFlags flags = gnm->flags;
        GnmCommandCallbackFunc callback = vk_ps4_command_overflow;
        *gnm = sceGnmCmdInit(next->mapped, cmd->pm4_buffer_size * sizeof(uint32_t), &callback, cmd);
        gnm->flags = flags;
        gnm->_unused = 0;
        gnm->_unused2 = 0;
        cmd->pm4_segment_count = count + 1u;
        return true;
    }
exhausted:
    cmd->recording_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    vk_ps4_log("PM4 capacity/allocation failure: requested=%u segments=%u/%u; recording rejected",
               requested, count, VK_PS4_MAX_PM4_SEGMENTS);
    return false;
}

/* Dynamic-state entry points are also used to emit the corresponding static
 * rasterization values when a graphics pipeline is bound. */
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetDepthBias(
    VkCommandBuffer commandBuffer, float depthBiasConstantFactor,
    float depthBiasClamp, float depthBiasSlopeFactor
);
VKAPI_ATTR void VKAPI_CALL vk_ps4_CmdSetLineWidth(
    VkCommandBuffer commandBuffer, float lineWidth
);

/* Check if a VkFormat has a depth component. */
static bool vk_format_has_depth(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return true;
    default:
        return false;
    }
}

/* Check if a VkFormat has a stencil component. */
static bool vk_format_has_stencil(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
    case VK_FORMAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

/* === Small-level texture tiling ===
 *
 * A mip level smaller than a macro tile is read by the hardware through the
 * 1D thin tile mode whatever the texture's base mode says (GpuAddress calls
 * this the adjusted mode; sceGpaComputeSurfaceInfo reports it). The address
 * library's tiler follows the parameters it is handed: with the base 2D mode
 * it computes a footprint larger than the level's allocation and reports an
 * overflow for every level of 64x64 and below, and its 1D path rejects
 * levels of 16x16 and below outright (B4 test 9: every scene texture upload
 * batch rejected, -8). This is the 1D thin layout for 32-bit texels - 8x8
 * micro tiles in row-major order, texel bits x0 x1 y0 x2 y1 y2 - verified
 * byte for byte against the library on the 64x64 and 32x32 levels it does
 * accept (tests/mip_tiling_test.c). Levels below 8x8 occupy one micro tile;
 * the texels outside the level stay zero and are never sampled. */
static void vk_ps4_tile_1d_thin_32bpp(const uint8_t *src, uint32_t width, uint32_t height,
                                      uint8_t *dst, size_t dst_len) {
    const uint32_t tiles_per_row = (width + 7u) / 8u;
    memset(dst, 0, dst_len);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t mx = x & 7u, my = y & 7u;
            const uint32_t texel = (mx & 1u) | (((mx >> 1) & 1u) << 1) | ((my & 1u) << 2) |
                                   (((mx >> 2) & 1u) << 3) | (((my >> 1) & 1u) << 4) |
                                   (((my >> 2) & 1u) << 5);
            const size_t tile = (size_t)(y / 8u) * tiles_per_row + (x / 8u);
            const size_t offset = tile * 256u + (size_t)texel * 4u;
            if (offset + 4u <= dst_len)
                memcpy(dst + offset, src + ((size_t)y * width + x) * 4u, 4u);
        }
    }
}

/* === PM4 helpers for direct context register emission === */

/* Emit a single PM4 SET_CONTEXT_REG packet.
 * Used for registers that don't have GNM API wrappers
 * (stencil ref/mask, stencil ops, stencil clear, depth bounds). */
static void vk_ps4_emit_context_reg(GnmCommandBuffer *cmd, uint32_t reg_addr, uint32_t value) {
    /* Validate register address is in context register space */
    if (reg_addr < SI_CONTEXT_REG_OFFSET || reg_addr >= SI_CONTEXT_REG_END) {
        return;  /* Invalid register address — silently skip */
    }

    const uint32_t num_dwords = 3;  /* header + reg offset + value */

    /* Try to resize if not enough space (like GNM's setcontextregister does) */
    if ((uint32_t)(cmd->endptr - cmd->cmdptr) < num_dwords) {
        if (cmd->callback.func) {
            cmd->callback.func(cmd, num_dwords, cmd->callback.userdata);
        }
        if ((uint32_t)(cmd->endptr - cmd->cmdptr) < num_dwords) {
            return;  /* Still not enough space — silently skip */
        }
    }

    cmd->cmdptr[0] = PKT3(PKT3_SET_CONTEXT_REG, 1, 0);
    cmd->cmdptr[1] = (reg_addr - SI_CONTEXT_REG_OFFSET) >> 2;
    cmd->cmdptr[2] = value;
    cmd->cmdptr += num_dwords;
}


/* Vulkan's fixed-function state must not inherit GNM's firmware defaults or
 * the preceding meta-clear. Blend factors and channel masks do not enable
 * color-buffer writes: CB_COLOR_CONTROL.MODE/ROP3 is a separate register. */
static void vk_ps4_emit_color_control(GnmCommandBuffer *cmd,
                                      const VkPs4Pipeline *pipe) {
    static const uint8_t rop3[16] = {
        0x00, 0x88, 0x44, 0xcc, 0x22, 0xaa, 0x66, 0xee,
        0x11, 0x99, 0x55, 0xdd, 0x33, 0xbb, 0x77, 0xff
    };
    uint32_t rop = V_028808_ROP3_COPY;
    if (pipe && pipe->color_blend_state.logicOpEnable &&
        (uint32_t)pipe->color_blend_state.logicOp < 16u)
        rop = rop3[pipe->color_blend_state.logicOp];
    vk_ps4_emit_context_reg(cmd, R_028808_CB_COLOR_CONTROL,
        S_028808_MODE(V_028808_CB_NORMAL) | S_028808_ROP3(rop));
}

static void vk_ps4_emit_clip_control(GnmCommandBuffer *cmd,
                                     const VkPs4Pipeline *pipe) {
    const bool clamp = pipe && pipe->rasterization_state.depthClampEnable;
    vk_ps4_emit_context_reg(cmd, R_028810_PA_CL_CLIP_CNTL,
        S_028810_DX_CLIP_SPACE_DEF(1) |
        S_028810_ZCLIP_NEAR_DISABLE(clamp) |
        S_028810_ZCLIP_FAR_DISABLE(clamp));
}

/* A load-op clear runs before the application's first vkCmdSetViewport.
 * Explicitly set its viewport and disable culling, which may otherwise be
 * left at zero size or inherited from a prior shadow/world pipeline. */
static void vk_ps4_prepare_clear_draw(GnmCommandBuffer *cmd,
                                      uint32_t width, uint32_t height) {
    GnmSetViewportInfo viewport = {0};
    viewport.dmax = 1.0f;
    viewport.scale[0] = viewport.offset[0] = width * 0.5f;
    viewport.scale[1] = viewport.offset[1] = height * 0.5f;
    viewport.scale[2] = 1.0f;
    sceGnmDrawCmdSetViewport(cmd, 0, &viewport);
    GnmPrimitiveSetup primitive = {0};
    primitive.cullmode = GNM_CULL_NONE;
    primitive.frontmode = primitive.backmode = GNM_FILL_SOLID;
    primitive.frontface = GNM_FACE_CCW;
    primitive.provokemode = GNM_PROVOKINGVTX_FIRST;
    sceGnmDrawCmdSetPrimitiveSetup(cmd, &primitive);
    sceGnmDrawCmdSetPrimitiveType(cmd, GNM_PT_TRILIST);
    vk_ps4_emit_clip_control(cmd, NULL);
}

/* Program the PS interpolator mapping explicitly from the compiler semantic
 * tables.  On Liverpool/FW 5.05 the opaque OpenGNM helper can leave the PS
 * reading the wrong VS export slot even though semantic IDs match.  The
 * result is characteristic position/register data showing up as saturated
 * RGB facets.  SPI_PS_INPUT_CNTL_n.OFFSET is the VS export *outindex*, not
 * the semantic number. */
static void vk_ps4_emit_ps_input_linkage(
    GnmCommandBuffer *cmd,
    const GnmVertexExportSemantic *vs_exports, uint32_t vs_export_count,
    const GnmPixelInputSemantic *ps_inputs, uint32_t ps_input_count) {
    vk_ps4_emit_context_reg(cmd, R_0286D4_SPI_INTERP_CONTROL_0,
        vk_ps4_ps_interpolator_control(
            vk_ps4_ps_uses_point_coord(ps_inputs, ps_input_count)));
    for (uint32_t p = 0; p < ps_input_count; ++p) {
        const GnmPixelInputSemantic *ps = &ps_inputs[p];
        uint32_t input_cntl;
        /* Pipeline creation already rejects missing links.  Keep this guard
         * anyway so a malformed pipeline can never silently map to export 0. */
        if (!vk_ps4_ps_input_control(vs_exports, vs_export_count, ps,
                                    &input_cntl)) {
            continue;
        }
        vk_ps4_emit_context_reg(cmd,
                                R_028644_SPI_PS_INPUT_CNTL_0 + p * 4u,
                                input_cntl);
    }
}

/* === Vulkan-to-GNM enum mappings for depth/stencil === */

/* VkStencilOp → PM4 V_02842C_STENCIL_* values. */
uint32_t vk_stencil_op_to_pm4(VkStencilOp op) {
    switch (op) {
    case VK_STENCIL_OP_KEEP:                return V_02842C_STENCIL_KEEP;       /* 0 */
    case VK_STENCIL_OP_ZERO:                return V_02842C_STENCIL_ZERO;       /* 1 */
    case VK_STENCIL_OP_REPLACE:             return V_02842C_STENCIL_REPLACE_TEST;/* 3 */
    case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return V_02842C_STENCIL_ADD_CLAMP;  /* 5 */
    case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return V_02842C_STENCIL_SUB_CLAMP;  /* 6 */
    case VK_STENCIL_OP_INVERT:              return V_02842C_STENCIL_INVERT;     /* 7 */
    case VK_STENCIL_OP_INCREMENT_AND_WRAP:  return V_02842C_STENCIL_ADD_WRAP;   /* 8 */
    case VK_STENCIL_OP_DECREMENT_AND_WRAP:  return V_02842C_STENCIL_SUB_WRAP;   /* 9 */
    default:                                return V_02842C_STENCIL_KEEP;
    }
}

/* Convert float to uint32 bit pattern (like fui()). */
static inline uint32_t vk_ps4_fui(float f) {
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

/* Convert float32 to float16 (IEEE 754 half precision).
 * Implements the standard round-to-nearest-even conversion. */
static inline uint16_t vk_ps4_float_to_half(float f) {
    union { float f; uint32_t u; } v;
    v.f = f;
    uint32_t x = v.u;
    uint32_t sign = (x >> 31) & 1;
    uint32_t exp = (x >> 23) & 0xFF;
    uint32_t mant = x & 0x7FFFFF;

    if (exp == 0xFF) {
        /* Inf or NaN → half Inf/NaN */
        if (mant == 0) {
            return (uint16_t)((sign << 15) | 0x7C00);
        } else {
            /* NaN: preserve mantissa bits, set at least one */
            return (uint16_t)((sign << 15) | 0x7C00 | (mant >> 13) | 1);
        }
    }

    /* Bias adjust: 127 → 15 */
    int32_t new_exp = (int32_t)exp - 127 + 15;

    if (new_exp <= 0) {
        /* Underflow to denormal or zero */
        if (new_exp < -10) {
            /* Too small → zero */
            return (uint16_t)(sign << 15);
        }
        /* Denormal: shift mantissa by (14 - new_exp) bits with rounding */
        mant = mant | 0x800000;  /* implicit 1 */
        uint32_t shift = (uint32_t)(14 - new_exp);
        uint32_t result_mant = mant >> shift;
        /* Round-to-nearest-even */
        uint32_t round_bit = (mant >> (shift - 1)) & 1;
        uint32_t sticky = (shift > 1) ? ((mant & ((1U << (shift - 1)) - 1)) != 0) : 0;
        if (round_bit && (sticky || (result_mant & 1))) {
            result_mant++;
        }
        return (uint16_t)((sign << 15) | result_mant);
    }

    if (new_exp >= 0x1F) {
        /* Overflow → half Inf */
        return (uint16_t)((sign << 15) | 0x7C00);
    }

    /* Normal: pack sign, exponent, mantissa with rounding */
    uint32_t result = (sign << 15) | ((uint32_t)new_exp << 10) | (mant >> 13);
    /* Round-to-nearest-even based on truncated bits */
    uint32_t round_bit = (mant >> 12) & 1;
    uint32_t sticky = (mant & 0xFFF) != 0;
    if (round_bit && (sticky || (result & 1))) {
        result++;
    }
    return (uint16_t)result;
}

static float vk_ps4_clear_clamp(float value) {
    if (!(value > 0.0f)) return 0.0f;
    return value < 1.0f ? value : 1.0f;
}
static float vk_ps4_clear_srgb(float value) {
    value = vk_ps4_clear_clamp(value);
    return value <= 0.0031308f ? value * 12.92f :
        1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
}

/* Pack a VkClearColorValue into a 32-bit FillMemory value based on the format.
 * FillMemory writes 32-bit values to 4-byte-aligned addresses, so for
 * formats smaller than 32 bpp, the clear value is replicated to fill
 * the full 32-bit word. */
static uint32_t vk_ps4_pack_clear_val_32(VkFormat fmt, const VkClearColorValue *cc) {
    VkClearColorValue encoded = *cc;
    if (fmt == VK_FORMAT_R8G8B8A8_SRGB || fmt == VK_FORMAT_B8G8R8A8_SRGB ||
        fmt == VK_FORMAT_A8B8G8R8_SRGB_PACK32 || fmt == VK_FORMAT_R8_SRGB) {
        for (uint32_t c = 0; c < 3; ++c) encoded.float32[c] = vk_ps4_clear_srgb(cc->float32[c]);
        cc = &encoded;
    }
    uint32_t bpp = vk_format_to_bpp(fmt);

    switch (fmt) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32: {
        /* R8G8B8A8 memory layout: R,G,B,A bytes.
         * Pack float [0,1] → uint8 for UNORM formats. */
        uint8_t r = (uint8_t)(vk_ps4_clear_clamp(cc->float32[0]) * 255.0f + 0.5f);
        uint8_t g = (uint8_t)(vk_ps4_clear_clamp(cc->float32[1]) * 255.0f + 0.5f);
        uint8_t b = (uint8_t)(vk_ps4_clear_clamp(cc->float32[2]) * 255.0f + 0.5f);
        uint8_t a = (uint8_t)(vk_ps4_clear_clamp(cc->float32[3]) * 255.0f + 0.5f);
        return (uint32_t)r | ((uint32_t)g << 8) |
               ((uint32_t)b << 16) | ((uint32_t)a << 24);
    }
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB: {
        /* B8G8R8A8 memory layout: B,G,R,A bytes — swap R and B. */
        uint8_t r = (uint8_t)(vk_ps4_clear_clamp(cc->float32[0]) * 255.0f + 0.5f);
        uint8_t g = (uint8_t)(vk_ps4_clear_clamp(cc->float32[1]) * 255.0f + 0.5f);
        uint8_t b = (uint8_t)(vk_ps4_clear_clamp(cc->float32[2]) * 255.0f + 0.5f);
        uint8_t a = (uint8_t)(vk_ps4_clear_clamp(cc->float32[3]) * 255.0f + 0.5f);
        return (uint32_t)b | ((uint32_t)g << 8) |
               ((uint32_t)r << 16) | ((uint32_t)a << 24);
    }
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R8G8B8A8_SNORM: {
        /* Integer formats: use uint32 components directly */
        uint8_t r = (uint8_t)(cc->uint32[0] & 0xFF);
        uint8_t g = (uint8_t)(cc->uint32[1] & 0xFF);
        uint8_t b = (uint8_t)(cc->uint32[2] & 0xFF);
        uint8_t a = (uint8_t)(cc->uint32[3] & 0xFF);
        return (uint32_t)r | ((uint32_t)g << 8) |
               ((uint32_t)b << 16) | ((uint32_t)a << 24);
    }
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8_SRGB: {
        /* 8-bit: replicate 4 times */
        uint8_t v = (uint8_t)(vk_ps4_clear_clamp(cc->float32[0]) * 255.0f + 0.5f);
        return (uint32_t)v | ((uint32_t)v << 8) |
               ((uint32_t)v << 16) | ((uint32_t)v << 24);
    }
    case VK_FORMAT_R16_SFLOAT: {
        /* 16-bit float: convert float32 → float16 (IEEE 754 half). */
        uint16_t h = vk_ps4_float_to_half(cc->float32[0]);
        return (uint32_t)h | ((uint32_t)h << 16);
    }
    case VK_FORMAT_R16_UNORM: {
        /* 16-bit UNORM: convert float [0,1] → uint16 [0,65535]. */
        uint16_t h = (uint16_t)(vk_ps4_clear_clamp(cc->float32[0]) * 65535.0f + 0.5f);
        return (uint32_t)h | ((uint32_t)h << 16);
    }
    case VK_FORMAT_R16G16_SFLOAT: {
        /* 32-bit = two float16 values */
        uint16_t r = vk_ps4_float_to_half(cc->float32[0]);
        uint16_t g = vk_ps4_float_to_half(cc->float32[1]);
        return (uint32_t)r | ((uint32_t)g << 16);
    }
    case VK_FORMAT_R16G16_UNORM: {
        /* 32-bit = two uint16 UNORM values */
        uint16_t r = (uint16_t)(vk_ps4_clear_clamp(cc->float32[0]) * 65535.0f + 0.5f);
        uint16_t g = (uint16_t)(vk_ps4_clear_clamp(cc->float32[1]) * 65535.0f + 0.5f);
        return (uint32_t)r | ((uint32_t)g << 16);
    }
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT: {
        /* 32-bit = two 16-bit integer values */
        uint16_t r = (uint16_t)cc->uint32[0];
        uint16_t g = (uint16_t)cc->uint32[1];
        return (uint32_t)r | ((uint32_t)g << 16);
    }
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_D32_SFLOAT:
        return cc->uint32[0];
    default:
        /* For 32-bit formats, use first float component bits */
        if (bpp == 4) return vk_ps4_fui(cc->float32[0]);
        /* For other sizes, replicate the first byte */
        if (bpp == 1) {
            uint8_t v = (uint8_t)vk_ps4_fui(cc->float32[0]);
            return (uint32_t)v | ((uint32_t)v << 8) |
                   ((uint32_t)v << 16) | ((uint32_t)v << 24);
        }
        if (bpp == 2) {
            uint16_t v = (uint16_t)vk_ps4_fui(cc->float32[0]);
            return (uint32_t)v | ((uint32_t)v << 16);
        }
        return vk_ps4_fui(cc->float32[0]);
    }
}

/* Draw-based color clear using the embedded clear pixel shader.
 * This is the correct clear path for tiled render targets — FillMemory
 * writes linearly and doesn't respect the tiled memory layout, but a
 * draw-based clear goes through the CB hardware which handles tiling.
 *
 * The clear PS reads one vec4 UBO from a set0 descriptor table. Compiler
 * layout, descriptor bytes and the metadata-reported pointer SGPR match. */
/* Standalone VS builtins may use base-vertex SGPRs even without resources.
 * Clear draws always start at zero; do not inherit values from scene draws.
 * The clear path invalidates graphics state so subsequent draws restore it. */
static void vk_ps4_bind_clear_vs(VkPs4Device *device, GnmCommandBuffer *cmd) {
    sceGnmDrawCmdSetVsShader(cmd, &device->clear_vs_regs, 0);
    for (uint32_t i = 0; i < 16; i += 2)
        sceGnmDrawCmdSetPointerUserData(cmd, GNM_STAGE_VS, i, NULL);
}

static void vk_ps4_clear_color_draw(VkPs4CommandBuffer *cmd,
                                     VkPs4Image *img,
                                     const VkClearColorValue *cc,
                                     const VkRect2D *rect) {
    VkPs4Device *dev = cmd->device;
    if (!dev || !dev->clear_ps_ready) {
        vk_ps4_command_fail(cmd, "clear shader unavailable");
        return;
    }

    /* Command-owned memory survives until GPU completion, unlike stack
     * color/descriptor storage. CmdAllocInside embeds skipped inline data. */
    if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) < 32u) {
        if (!vk_ps4_command_overflow(&cmd->gnm_cmd, 32, cmd)) return;
    }
    struct ClearUniform { uint32_t color[4]; GnmBuffer descriptor; };
    struct ClearUniform *uniform = sceGnmCmdAllocInside(&cmd->gnm_cmd,
        sizeof(struct ClearUniform), 16);
    if (!uniform || ((uint64_t)(uintptr_t)&uniform->descriptor >> 32u) !=
            VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI) {
        vk_ps4_command_fail(cmd, "clear UBO allocation outside compiled descriptor address window");
        return;
    }
    memcpy(uniform->color, cc->uint32, sizeof(uniform->color));
    uniform->descriptor = sceGnmCreateConstBuffer(uniform->color, sizeof(uniform->color));
    vk_ps4_cpu_store_fence();
    /* GFX7 RADV/PSBC carries the low pointer dword in this SGPR and embeds
     * address32_hi in the code. Do not overwrite the adjacent user SGPR. */
    uint32_t *packet = cmd->gnm_cmd.cmdptr;
    packet[0] = PKT3(PKT3_SET_SH_REG, 1, 0);
    packet[1] = (R_00B030_SPI_SHADER_USER_DATA_PS_0 - SI_SH_REG_OFFSET) / 4u +
                dev->clear_ps_table_reg;
    packet[2] = (uint32_t)(uintptr_t)&uniform->descriptor;
    cmd->gnm_cmd.cmdptr += 3;

    /* Bind the RT being cleared at slot 0.  The clear PS outputs to
     * MRT0, so the target must be at slot 0 regardless of which slot
     * it was originally bound to by vk_ps4_bind_subpass_targets.
     * The original bindings are restored after all clears by a
     * re-call to vk_ps4_bind_subpass_targets. */
    sceGnmDrawCmdSetRenderTarget(&cmd->gnm_cmd, 0, &img->gnm_rt);

    /* Disable blending for RT0 so the clear color overwrites the RT
     * instead of being blended with existing contents.  The previous
     * pipeline's blend state is restored by vk_ps4_rebind_pipeline_state
     * or the next CmdBindPipeline. */
    GnmBlendControl no_blend;
    memset(&no_blend, 0, sizeof(no_blend));
    no_blend.blendenabled = false;
    sceGnmDrawCmdSetBlendControl(&cmd->gnm_cmd, 0, &no_blend);

    /* Set scissor to cover the full RT so the clear draw isn't clipped. */
    sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd,
        rect->offset.x, rect->offset.y,
        rect->offset.x + rect->extent.width,
        rect->offset.y + rect->extent.height);

    vk_ps4_prepare_clear_draw(&cmd->gnm_cmd,
        img->create_info.extent.width, img->create_info.extent.height);
    vk_ps4_emit_color_control(&cmd->gnm_cmd, NULL);
    sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd, 0x0fu);
    GnmDepthStencilControl depth_off = {0};
    sceGnmDrawCmdSetDepthStencilControl(&cmd->gnm_cmd, &depth_off);

    /* Bind the clear pixel shader and fullscreen VS */
    sceGnmDrawCmdSetPsShader(&cmd->gnm_cmd, &dev->clear_ps_regs);
    vk_ps4_bind_clear_vs(cmd->device, &cmd->gnm_cmd);

    /* Draw a fullscreen triangle to clear the entire RT.
     * Reset instance count to 1 — VGT_INSTANCE_COUNT is sticky. */
    sceGnmDrawCmdSetNumInstances(&cmd->gnm_cmd, 1);
    sceGnmDrawCmdDrawIndexAuto(&cmd->gnm_cmd, 3);
}

/* Draw-based depth/stencil clear using embedded fullscreen VS + dummy PS.
 * Triggers the GCN lazy clear by doing a draw that accesses depth.
 * Temporarily enables depth write to guarantee the lazy clear fires,
 * even if the currently bound pipeline has depth disabled. */
static void vk_ps4_clear_depth_draw(VkPs4Device *device, GnmCommandBuffer *cmd,
                                     uint32_t width, uint32_t height) {
    vk_ps4_prepare_clear_draw(cmd, width, height);
    /* Depth-only clear must never replace a scene's color pixels. */
    sceGnmDrawCmdSetRenderTargetMask(cmd, 0);
    /* Temporarily enable depth write so the draw accesses the depth buffer.
     * This guarantees the lazy clear fires regardless of the bound pipeline's
     * depth state. DB_DEPTH_CONTROL is restored by the next CmdBindPipeline. */
    GnmDepthStencilControl ds_write;
    memset(&ds_write, 0, sizeof(ds_write));
    ds_write.depthenable = 1;
    ds_write.zwrite = 1;
    ds_write.zfunc = GNM_DEPTH_COMPARE_ALWAYS;
    ds_write.stencilenable = 0;
    ds_write.depthboundsenable = 0;
    sceGnmDrawCmdSetDepthStencilControl(cmd, &ds_write);

    /* Use embedded fullscreen VS + dummy PS */
    vk_ps4_bind_clear_vs(device, cmd);
    sceGnmDrawCmdSetEmbeddedPsShader(cmd, GNM_EMBEDDED_PSH_DUMMY);

    /* Draw a fullscreen triangle to trigger the lazy clear.
     * Reset instance count to 1 — VGT_INSTANCE_COUNT is sticky and may
     * have been set to a large value by a previous draw. */
    sceGnmDrawCmdSetPrimitiveType(cmd, GNM_PT_TRILIST);
    sceGnmDrawCmdSetNumInstances(cmd, 1);
    sceGnmDrawCmdDrawIndexAuto(cmd, 3);

    /* DB_RENDER_CONTROL is sticky.  DEPTH_CLEAR_ENABLE must describe only the
     * fullscreen clear draw; leaving it enabled until CmdEndRenderPass turns
     * subsequent application draws into additional clear operations and makes
     * depth ordering appear to flash.  PM4 ordering guarantees the reset is
     * observed only after the clear draw has consumed the enabled state. */
    GnmDbRenderControl db_ctrl;
    memset(&db_ctrl, 0, sizeof(db_ctrl));
    sceGnmDrawCmdSetDbRenderControl(cmd, &db_ctrl);
}

/* Clear exact layer/rectangle coverage. Normal scissored depth writes are
 * used for partial clears: a lazy metadata clear can affect a larger tile. */
static void vk_ps4_clear_region(VkPs4CommandBuffer *cmd, VkPs4Image *img,
                                VkImageAspectFlags aspects, const VkClearValue *value,
                                const VkRect2D *rect, uint32_t base_layer,
                                uint32_t layer_count) {
    uint32_t checked_layers;
    if (!img || !vk_ps4_rect_in_surface(rect, img->create_info.extent.width,
                                       img->create_info.extent.height) ||
        !vk_ps4_resolve_range(base_layer, layer_count, img->create_info.arrayLayers,
                             &checked_layers)) {
        vk_ps4_command_fail(cmd, "clear rectangle/layer out of range");
        return;
    }
    if (!rect->extent.width || !rect->extent.height) return;
    const bool depth = (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
    const bool stencil = (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
    if ((depth || stencil) ? (!img->is_depth_target ||
            (depth && !vk_format_has_depth(img->create_info.format)) ||
            (stencil && !vk_format_has_stencil(img->create_info.format))) :
            (!img->is_render_target || aspects != VK_IMAGE_ASPECT_COLOR_BIT ||
             img->gnm_rt.info.is_int)) {
        vk_ps4_command_fail(cmd, "clear format/aspect unsupported");
        return;
    }
    for (uint32_t layer = 0; layer < checked_layers; ++layer) {
        VkPs4Image target = *img;
        const uint32_t slice = base_layer + layer;
        if (!depth && !stencil) {
            target.gnm_rt.view.slicestart = slice;
            target.gnm_rt.view.slicemax = slice;
            vk_ps4_clear_color_draw(cmd, &target, &value->color, rect);
            continue;
        }
        target.gnm_drt.depthview.slicestart = slice;
        target.gnm_drt.depthview.slicemax = slice;
        sceGnmDrawCmdSetDepthRenderTarget(&cmd->gnm_cmd, &target.gnm_drt);
        sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd, rect->offset.x, rect->offset.y,
            rect->offset.x + rect->extent.width, rect->offset.y + rect->extent.height);
        /* Full depth-only clear retains the GPU's efficient clear operation. */
        const bool full = rect->offset.x == 0 && rect->offset.y == 0 &&
            rect->extent.width == img->create_info.extent.width &&
            rect->extent.height == img->create_info.extent.height;
        if (full && depth && !stencil) {
            sceGnmDrawCmdSetDepthClearValue(&cmd->gnm_cmd, value->depthStencil.depth);
            GnmDbRenderControl db = {0}; db.depthclearenable = true;
            sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd, &db);
            vk_ps4_clear_depth_draw(cmd->device, &cmd->gnm_cmd, img->create_info.extent.width,
                                    img->create_info.extent.height);
            continue;
        }
        GnmDbRenderControl db = {0};
        sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd, &db);
        vk_ps4_prepare_clear_draw(&cmd->gnm_cmd, img->create_info.extent.width,
                                  img->create_info.extent.height);
        GnmSetViewportInfo viewport = {0};
        viewport.dmin = viewport.dmax = value->depthStencil.depth;
        viewport.offset[0] = viewport.scale[0] = img->create_info.extent.width * 0.5f;
        viewport.offset[1] = viewport.scale[1] = img->create_info.extent.height * 0.5f;
        viewport.offset[2] = value->depthStencil.depth;
        sceGnmDrawCmdSetViewport(&cmd->gnm_cmd, 0, &viewport);
        sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd, 0);
        GnmDepthStencilControl ds = {0};
        ds.depthenable = depth; ds.zwrite = depth; ds.zfunc = GNM_DEPTH_COMPARE_ALWAYS;
        ds.stencilenable = stencil; ds.stencilfunc = GNM_DEPTH_COMPARE_ALWAYS;
        ds.stencilbackfunc = GNM_DEPTH_COMPARE_ALWAYS;
        sceGnmDrawCmdSetDepthStencilControl(&cmd->gnm_cmd, &ds);
        if (stencil) {
            const uint32_t masks = S_028430_STENCILTESTVAL(value->depthStencil.stencil & 255u) |
                S_028430_STENCILMASK(255) | S_028430_STENCILWRITEMASK(255);
            vk_ps4_emit_context_reg(&cmd->gnm_cmd, R_028430_DB_STENCILREFMASK, masks);
            vk_ps4_emit_context_reg(&cmd->gnm_cmd, R_028434_DB_STENCILREFMASK_BF, masks);
            vk_ps4_emit_context_reg(&cmd->gnm_cmd, R_02842C_DB_STENCIL_CONTROL,
                S_02842C_STENCILFAIL(V_02842C_STENCIL_REPLACE_TEST) |
                S_02842C_STENCILZPASS(V_02842C_STENCIL_REPLACE_TEST) |
                S_02842C_STENCILZFAIL(V_02842C_STENCIL_REPLACE_TEST));
        }
        vk_ps4_bind_clear_vs(cmd->device, &cmd->gnm_cmd);
        sceGnmDrawCmdSetEmbeddedPsShader(&cmd->gnm_cmd, GNM_EMBEDDED_PSH_DUMMY);
        sceGnmDrawCmdSetNumInstances(&cmd->gnm_cmd, 1);
        sceGnmDrawCmdDrawIndexAuto(&cmd->gnm_cmd, 3);
    }
}

/* Bind render targets for the current subpass.
 * Uses the subpass description's pColorAttachments to map framebuffer
 * attachment indices to RT slots, and pDepthStencilAttachment for depth. */
/* Get the effective attachment view for a given index.
 * For imageless framebuffers, uses the views from VkRenderPassAttachmentBeginInfo
 * stored in cmd->current_render_pass.imageless_attachments.
 * For regular framebuffers, uses fb->attachments. */
static VkPs4ImageView *vk_ps4_get_attachment_view(VkPs4CommandBuffer *cmd, uint32_t att_idx) {
    VkPs4Framebuffer *fb = cmd->current_render_pass.framebuffer;
    if (!fb || att_idx >= fb->attachment_count) return NULL;
    if (fb->imageless) {
        if (att_idx >= cmd->current_render_pass.imageless_attachment_count)
            return NULL;
        return cmd->current_render_pass.imageless_attachments[att_idx];
    }
    return fb->attachments[att_idx];
}

static void vk_ps4_bind_subpass_targets(VkPs4CommandBuffer *cmd) {
    VkPs4RenderPass *rp = cmd->current_render_pass.pass;
    VkPs4Framebuffer *fb = cmd->current_render_pass.framebuffer;
    uint32_t subpass_idx = cmd->current_render_pass.current_subpass;

    if (!rp || !fb || subpass_idx >= rp->subpass_count) return;

    const VkSubpassDescription *subpass = &rp->subpasses[subpass_idx];

    /* A depth-only pass must not retain the prior scene's color targets;
     * the color-only overlay must likewise detach the scene depth target. */
    for (uint32_t slot = 0; slot < 8; ++slot) {
        const GnmRenderTarget *target = NULL;
        if (subpass->pColorAttachments && slot < subpass->colorAttachmentCount) {
            uint32_t index = subpass->pColorAttachments[slot].attachment;
            if (index != VK_ATTACHMENT_UNUSED) {
                VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, index);
                if (view && view->image && view->image->is_render_target)
                    target = &view->gnm_rt_view;
            }
        }
        sceGnmDrawCmdSetRenderTarget(&cmd->gnm_cmd, slot, target);
    }
    const GnmDepthRenderTarget *depth = NULL;
    if (subpass->pDepthStencilAttachment) {
        uint32_t index = subpass->pDepthStencilAttachment->attachment;
        if (index != VK_ATTACHMENT_UNUSED) {
            VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, index);
            if (view && view->image && view->image->is_depth_target)
                depth = &view->gnm_drt_view;
        }
    }
    sceGnmDrawCmdSetDepthRenderTarget(&cmd->gnm_cmd, depth);
}

/* Look up the framebuffer attachment index for a given color RT slot
 * in the current subpass. Returns VK_ATTACHMENT_UNUSED if not found. */
static uint32_t vk_ps4_subpass_color_attachment(VkPs4CommandBuffer *cmd, uint32_t rt_slot) {
    VkPs4RenderPass *rp = cmd->current_render_pass.pass;
    uint32_t subpass_idx = cmd->current_render_pass.current_subpass;

    if (!rp || subpass_idx >= rp->subpass_count) return VK_ATTACHMENT_UNUSED;
    if (!rp->subpasses[subpass_idx].pColorAttachments) return VK_ATTACHMENT_UNUSED;
    if (rt_slot >= rp->subpasses[subpass_idx].colorAttachmentCount) return VK_ATTACHMENT_UNUSED;

    return rp->subpasses[subpass_idx].pColorAttachments[rt_slot].attachment;
}

/* Look up the framebuffer attachment index for the depth/stencil attachment
 * in the current subpass. Returns VK_ATTACHMENT_UNUSED if not found. */
static uint32_t vk_ps4_subpass_depth_attachment(VkPs4CommandBuffer *cmd) {
    VkPs4RenderPass *rp = cmd->current_render_pass.pass;
    uint32_t subpass_idx = cmd->current_render_pass.current_subpass;

    if (!rp || subpass_idx >= rp->subpass_count) return VK_ATTACHMENT_UNUSED;
    if (!rp->subpasses[subpass_idx].pDepthStencilAttachment) return VK_ATTACHMENT_UNUSED;

    return rp->subpasses[subpass_idx].pDepthStencilAttachment->attachment;
}

/* Deferred user-data state (see VkPs4CommandBuffer::graphics_tables_dirty). */
static void vk_ps4_reset_user_data_state(VkPs4CommandBuffer *cmd) {
    memset(cmd->graphics_push_dirty, 0, sizeof(cmd->graphics_push_dirty));
    memset(cmd->compute_push_constants, 0, sizeof(cmd->compute_push_constants));
    cmd->compute_push_valid = false;
    cmd->compute_push_dirty = false;
    cmd->graphics_tables_dirty = false;
    cmd->compute_tables_dirty = false;
    cmd->graphics_dynamic_table = NULL;
    cmd->compute_dynamic_table = NULL;
    cmd->vertex_table_pipeline = NULL;
}

/* Re-emit the current pipeline's graphics state after it has been clobbered
 * by a draw-based clear. This restores VS/PS shaders, blend state, RT mask,
 * depth/stencil control, and primitive type so the app can continue drawing
 * without re-binding its pipeline (per Vulkan spec for CmdClearAttachments). */
static void vk_ps4_rebind_pipeline_state(VkPs4CommandBuffer *cmd) {
    VkPs4Pipeline *pipe = cmd->current_pipeline;
    const uint32_t stencil_front = cmd->stencil_refmask_front;
    const uint32_t stencil_back = cmd->stencil_refmask_back;
    const bool stencil_valid = cmd->stencil_shadow_valid;
    /* The clear PS overwrites user SGPRs 0..3. Re-binding the pipeline marks
     * the set tables, push constants and vertex table for re-emission at
     * the next draw, which is what restores them. */
    if (pipe && pipe->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS)
        vk_ps4_CmdBindPipeline((VkCommandBuffer)cmd,
                              VK_PIPELINE_BIND_POINT_GRAPHICS, (VkPipeline)pipe);
    if (stencil_valid) {
        cmd->stencil_refmask_front = stencil_front;
        cmd->stencil_refmask_back = stencil_back;
        cmd->stencil_shadow_valid = true;
        vk_ps4_emit_context_reg(&cmd->gnm_cmd, R_028430_DB_STENCILREFMASK, stencil_front);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd, R_028434_DB_STENCILREFMASK_BF, stencil_back);
    }
    if (cmd->viewport0_valid)
        vk_ps4_CmdSetViewport((VkCommandBuffer)cmd, 0, 1, &cmd->viewport0);
    if (cmd->scissor0_valid)
        vk_ps4_CmdSetScissor((VkCommandBuffer)cmd, 0, 1, &cmd->scissor0);
}

/* Helper: compute bytes-per-pixel for a VkFormat (for copy operations).
 * For compressed formats, returns bytes-per-block (not bytes-per-pixel).
 * Use vk_format_is_compressed() to detect block-compressed formats. */
static uint32_t vk_format_to_bpp(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_R8_UNORM:           return 1;
    case VK_FORMAT_R8_SNORM:           return 1;
    case VK_FORMAT_R8_UINT:            return 1;
    case VK_FORMAT_R8_SINT:            return 1;
    case VK_FORMAT_R8_SRGB:            return 1;
    case VK_FORMAT_R8G8_UNORM:         return 2;
    case VK_FORMAT_R8G8_SNORM:         return 2;
    case VK_FORMAT_R8G8_UINT:          return 2;
    case VK_FORMAT_R8G8_SINT:          return 2;
    case VK_FORMAT_R8G8_SRGB:          return 2;
    case VK_FORMAT_R8G8B8_UNORM:       return 3;
    case VK_FORMAT_R8G8B8A8_UNORM:     return 4;
    case VK_FORMAT_B8G8R8A8_UNORM:     return 4;
    case VK_FORMAT_R8G8B8A8_SNORM:     return 4;
    case VK_FORMAT_R8G8B8A8_UINT:      return 4;
    case VK_FORMAT_R8G8B8A8_SINT:      return 4;
    case VK_FORMAT_R8G8B8A8_SRGB:      return 4;
    case VK_FORMAT_B8G8R8A8_SRGB:      return 4;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return 4;
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:  return 4;
    case VK_FORMAT_R16_SFLOAT:         return 2;
    case VK_FORMAT_R16_UNORM:          return 2;
    case VK_FORMAT_R16G16_SFLOAT:      return 4;
    case VK_FORMAT_R16G16_UNORM:       return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
    case VK_FORMAT_R16G16B16A16_UNORM:  return 8;
    case VK_FORMAT_R32_SFLOAT:         return 4;
    case VK_FORMAT_R32_UINT:           return 4;
    case VK_FORMAT_R32G32_SFLOAT:      return 8;
    case VK_FORMAT_R32G32_UINT:        return 8;
    case VK_FORMAT_R32G32B32_SFLOAT:   return 12;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
    case VK_FORMAT_R32G32B32A32_UINT:   return 16;
    case VK_FORMAT_D16_UNORM:          return 2;
    case VK_FORMAT_D32_SFLOAT:         return 4;
    case VK_FORMAT_D24_UNORM_S8_UINT:  return 4;
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return 8;
    /* BC formats: bytes per 4x4 block */
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return 8;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:  return 8;
    case VK_FORMAT_BC2_UNORM_BLOCK:      return 16;
    case VK_FORMAT_BC3_UNORM_BLOCK:      return 16;
    case VK_FORMAT_BC7_UNORM_BLOCK:      return 16;
    case VK_FORMAT_BC7_SRGB_BLOCK:       return 16;
    default:                           return 4;
    }
}

/* Helper: check if a format is block-compressed (4x4 blocks). */
static bool vk_format_is_compressed(VkFormat fmt) {
    switch (fmt) {
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return true;
    default:
        return false;
    }
}

/* Helper: compute row size in bytes for a given format and width.
 * For uncompressed formats: width * bpp.
 * For compressed formats: (width / 4) * bytes_per_block (rounded up). */
static uint32_t vk_format_row_size(VkFormat fmt, uint32_t width) {
    uint32_t bpp = vk_format_to_bpp(fmt);
    if (vk_format_is_compressed(fmt)) {
        uint32_t blocks_w = (width + 3) / 4;
        return blocks_w * bpp;
    }
    return width * bpp;
}

static bool vk_ps4_image_has_linear_storage(const VkPs4Image *img) {
    if (!img) return false;
    if (img->is_swapchain_image) return true;
    if (img->is_render_target) {
        GnmTileMode tm = (GnmTileMode)img->gnm_rt.attrib.tilemode_index;
        return tm == GNM_TM_DISPLAY_LINEAR_GENERAL ||
               tm == GNM_TM_DISPLAY_LINEAR_ALIGNED;
    }
    if (img->is_depth_target) return false;
    GnmTileMode tm = (GnmTileMode)img->gnm_texture.tilingindex;
    return tm == GNM_TM_DISPLAY_LINEAR_GENERAL ||
           tm == GNM_TM_DISPLAY_LINEAR_ALIGNED;
}

static bool vk_ps4_format_is_linear_rgba8(VkFormat fmt) {
    switch (fmt) {
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

/* Rebuild the fetch shader's V# table from the active pipeline semantics and
 * the currently bound Vulkan buffers.  OpenGNM loads one descriptor per input
 * semantic.  Building only one descriptor per binding breaks interleaved
 * position/normal/uv streams and leaves the fetch shader reading beyond the
 * table.  The destination itself is mapped Garlic direct memory. */
static void vk_ps4_rebuild_vertex_table(VkPs4CommandBuffer *cmd) {
    /* M6.2.9 binds once per material range.  Logging every successful table
     * rebuild would turn a 578-range scene into more than 1,100 synchronous
     * log lines per frame.  Keep a short bring-up trace and sparse soak-test
     * breadcrumbs; alignment rejects below remain unconditional. */
    static uint64_t vertex_table_log_serial = 0;
    vertex_table_log_serial++;
    const bool log_vertex_table_success = vertex_table_log_serial <= 8u || (vertex_table_log_serial % 32768u) == 0u;

    if (!cmd) return;
    cmd->vertex_table_pipeline = cmd->current_pipeline;
    if (!cmd->vertex_table_mem.mapped) return;

    cmd->vertex_descriptor_count = 0;
    cmd->vertex_buffers_dirty = false;

    VkPs4Pipeline *pipe = cmd->current_pipeline;
    if (!pipe || !pipe->has_fetch_shader || !pipe->has_vb_table_slot)
        return;

    const VkPipelineVertexInputStateCreateInfo *vi = &pipe->vertex_input_state;
    uint32_t semantic_count = pipe->vs_input_semantic_count;
    if (semantic_count > VK_PS4_MAX_VERTEX_DESCRIPTORS)
        semantic_count = VK_PS4_MAX_VERTEX_DESCRIPTORS;
    if (semantic_count == 0u) return;

    GnmBuffer candidate[VK_PS4_MAX_VERTEX_DESCRIPTORS];
    memset(candidate, 0, sizeof(candidate));

    uint32_t valid_count = 0;
    for (uint32_t s = 0; s < semantic_count; s++) {
        const VkVertexInputAttributeDescription *attr = NULL;
        const uint32_t semantic = pipe->vs_input_semantics[s].semantic;
        for (uint32_t a = 0; a < vi->vertexAttributeDescriptionCount; a++) {
            if (vi->pVertexAttributeDescriptions[a].location == semantic) {
                attr = &vi->pVertexAttributeDescriptions[a];
                break;
            }
        }
        if (!attr && s < vi->vertexAttributeDescriptionCount)
            attr = &vi->pVertexAttributeDescriptions[s];
        if (!attr || attr->binding >= VK_PS4_MAX_VERTEX_BINDINGS)
            continue;

        uint32_t stride = 0;
        for (uint32_t b = 0; b < vi->vertexBindingDescriptionCount; b++) {
            if (vi->pVertexBindingDescriptions[b].binding == attr->binding) {
                stride = vi->pVertexBindingDescriptions[b].stride;
                break;
            }
        }
        if (stride == 0) continue;

        VkPs4Buffer *buf =
            (VkPs4Buffer *)cmd->vertex_buffers[attr->binding].buffer;
        const VkDeviceSize bind_offset =
            cmd->vertex_buffers[attr->binding].offset;
        if (!buf || !buf->memory || !buf->memory->gnm_mem.mapped ||
            bind_offset > buf->create_info.size ||
            attr->offset > buf->create_info.size - bind_offset)
            continue;

        GnmDataFormat fmt = vk_ps4_vk_format_to_gnm_buffer(attr->format);
        if (fmt.asuint == GNM_FMT_INVALID.asuint) continue;
        const uint32_t element_size = sceGnmDfGetBytesPerElement(fmt);
        const VkDeviceSize available =
            buf->create_info.size - bind_offset - attr->offset;
        if (element_size == 0 || available < element_size) continue;

        /* GCN reads a vertex buffer element through a typed buffer load,
         * and in the kernel's dword alignment mode those need the element
         * at a four-byte boundary, whatever its size. The earlier rule
         * ("the element's natural alignment") had two effects on the
         * console (B4 test 9): a 12-byte vec3 - every position and normal
         * in the client - is not a power of two and was skipped without a
         * word, so no table with a vec3 in it was ever complete and no 3D
         * draw was ever issued; and the 16-byte tangents and bone weights
         * at 8-byte offsets were rejected. The M6.2.4 termination that
         * motivated the rule is not reproduced by this layout (the same
         * vec3+vec4 streams draw on the console through this rule now). */
        const uint32_t required_alignment = element_size >= 4u ? 4u : element_size;

        const uint64_t records64 =
            1u + (available - element_size) / stride;
        const uint32_t records = records64 > UINT32_MAX
            ? UINT32_MAX : (uint32_t)records64;
        void *gpu_addr = (char *)buf->memory->gnm_mem.mapped +
                         buf->memory_offset + bind_offset + attr->offset;
        if (((uintptr_t)gpu_addr & (required_alignment - 1u)) != 0u) {
            /* Once per hundred: a rejected layout repeats every draw of
             * every frame, and the same line 47,000 times said nothing new. */
            static unsigned reject_count = 0;
            if (reject_count++ % 100u == 0u)
                vk_ps4_log("vertex table: REJECT semantic=%u address=%p format=0x%08x element=%u required-align=%u offset=%u stride=%u (occurrence %u)",
                           s, gpu_addr, (unsigned)fmt.asuint, element_size,
                           required_alignment, (unsigned)attr->offset, stride, reject_count);
            valid_count = 0;
            break;
        }
        candidate[s] = sceGnmCreateVertexBuffer(
            gpu_addr, fmt, stride, records
        );
        if (s < 4u && log_vertex_table_success) {
            vk_ps4_log("vertex table: semantic=%u address=%p format=0x%08x element=%u align=%u stride=%u records=%u",
                       s, gpu_addr, (unsigned)fmt.asuint, element_size,
                       required_alignment, stride, records);
        }
        valid_count++;
    }

    /* Never submit a partially initialized table: a zero V# descriptor can
     * turn a missing binding into a GPU read from address zero. */
    if (valid_count == semantic_count) {
        uint32_t hash = 2166136261u;
        const unsigned char *bytes = (const unsigned char *)candidate;
        for (size_t i = 0; i < sizeof(candidate); ++i)
            hash = (hash ^ bytes[i]) * 16777619u;
        uint32_t bucket = hash & 8191u;
        GnmBuffer *gpu_base = (GnmBuffer *)cmd->vertex_table_mem.mapped;
        GnmBuffer *cpu_base = cmd->vertex_table_cpu_keys;
        if (!cpu_base) {
            cmd->gnm_vertex_buffers = NULL;
            cmd->vertex_table_overflow = true;
            vk_ps4_log("vertex table: CPU key cache unavailable; recording rejected");
            return;
        }
        for (uint32_t probe = 0; probe < 8192u; ++probe) {
            uint32_t cached = cmd->vertex_table_hash[bucket];
            if (!cached) break;
            const uint32_t slot = cached - 1u;
            const GnmBuffer *key = cpu_base +
                (size_t)slot * VK_PS4_MAX_VERTEX_DESCRIPTORS;
            if (!memcmp(key, candidate, sizeof(candidate))) {
                cmd->gnm_vertex_buffers = gpu_base +
                    (size_t)slot * VK_PS4_MAX_VERTEX_DESCRIPTORS;
                cmd->vertex_descriptor_count = semantic_count;
                cmd->vertex_buffers_dirty = true;
                return; /* Lookup never reads GPU-visible Garlic memory. */
            }
            bucket = (bucket + 1u) & 8191u;
        }
        if (cmd->vertex_table_slot_cursor >= cmd->vertex_table_slot_capacity ||
            cmd->vertex_table_hash[bucket]) {
            cmd->gnm_vertex_buffers = NULL;
            cmd->vertex_table_overflow = true;
            vk_ps4_log("vertex table: unique snapshot capacity exhausted cursor=%u capacity=%u",
                       cmd->vertex_table_slot_cursor, cmd->vertex_table_slot_capacity);
            return;
        }
        const uint32_t slot = cmd->vertex_table_slot_cursor++;
        cmd->gnm_vertex_buffers = gpu_base +
            (size_t)slot * VK_PS4_MAX_VERTEX_DESCRIPTORS;
        GnmBuffer *cpu_key = cpu_base +
            (size_t)slot * VK_PS4_MAX_VERTEX_DESCRIPTORS;
        memcpy(cpu_key, candidate, sizeof(candidate));
        memcpy(cmd->gnm_vertex_buffers, candidate, sizeof(candidate));
        cmd->vertex_table_hash[bucket] = slot + 1u;
        cmd->vertex_descriptor_count = semantic_count;
        cmd->vertex_buffers_dirty = true;
    }
    vk_ps4_cpu_store_fence();
}

/* === Command pool === */

static void vk_ps4_release_pm4_storage(
    VkPs4CommandBuffer *cmd, const VkAllocationCallbacks *alloc
) {
    if (!cmd) return;
    if (cmd->pm4_mem.allocated) {
        sceGnmDirectMemoryRelease(&cmd->pm4_mem);
    } else if (cmd->pm4_buffer) {
        /* Compatibility for command buffers made by generic host builds. */
        vk_ps4_free(alloc, cmd->pm4_buffer);
    }
    for (uint32_t i = 0; i < VK_PS4_MAX_PM4_SEGMENTS - 1u; ++i)
        if (cmd->pm4_extra[i].allocated) sceGnmDirectMemoryRelease(&cmd->pm4_extra[i]);
    memset(cmd->pm4_extra, 0, sizeof(cmd->pm4_extra));
    memset(cmd->pm4_segment_used, 0, sizeof(cmd->pm4_segment_used));
    cmd->pm4_segment_count = 0;
    cmd->pm4_buffer = NULL;
    cmd->pm4_buffer_size = 0;
    if (cmd->vertex_table_mem.allocated)
        sceGnmDirectMemoryRelease(&cmd->vertex_table_mem);
    if (cmd->vertex_table_cpu_keys)
        vk_ps4_free(alloc, cmd->vertex_table_cpu_keys);
    cmd->vertex_table_cpu_keys = NULL;
    cmd->gnm_vertex_buffers = NULL;
    cmd->vertex_table_slot_cursor = 0;
    memset(cmd->vertex_table_hash, 0, sizeof(cmd->vertex_table_hash));
    cmd->vertex_table_slot_capacity = 0;
    cmd->vertex_table_overflow = false;
    cmd->vertex_descriptor_count = 0;
    if (cmd->dynamic_table_mem.allocated)
        sceGnmDirectMemoryRelease(&cmd->dynamic_table_mem);
    cmd->dynamic_descriptor_tables = NULL;
    cmd->dynamic_table_cursor = 0;
    cmd->dynamic_table_overflow = false;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool) {
    if (!device || !pCreateInfo || !pCommandPool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    VkPs4CommandPool *pool = vk_ps4_alloc_zero(alloc, sizeof(*pool), 16);
    if (!pool) return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->type = VK_PS4_OBJ_COMMAND_POOL;
    pool->device = dev;
    pool->queue_family_index = pCreateInfo->queueFamilyIndex;
    pool->flags = pCreateInfo->flags;
    *pCommandPool = (VkCommandPool)pool;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator) {
    if (!device || !commandPool) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    /* Free all command buffers allocated from this pool */
    for (uint32_t i = 0; i < pool->command_buffer_count; i++) {
        VkPs4CommandBuffer *cmd = pool->command_buffers[i];
        if (cmd) {
            vk_ps4_release_pm4_storage(cmd, alloc);
            vk_ps4_free(alloc, cmd);
            pool->command_buffers[i] = NULL;
        }
    }
    pool->command_buffer_count = 0;

    /* Free all command buffers in the free list */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        if (pool->free_list[i]) {
            vk_ps4_release_pm4_storage(pool->free_list[i], alloc);
            vk_ps4_free(alloc, pool->free_list[i]);
            pool->free_list[i] = NULL;
        }
    }
    pool->free_count = 0;

    vk_ps4_free(alloc, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo,
                              VkCommandBuffer *pCommandBuffers) {
    if (!device || !pAllocateInfo || !pCommandBuffers) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)pAllocateInfo->commandPool;

    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
        VkPs4CommandBuffer *cmd = NULL;

        /* Try to reuse from the pool's free list first */
        if (pool && pool->free_count > 0) {
            cmd = pool->free_list[--pool->free_count];
            pool->free_list[pool->free_count] = NULL;
            /* Reset the command buffer state for reuse */
            memset(cmd, 0, offsetof(VkPs4CommandBuffer, gnm_cmd));
            cmd->pm4_used = 0;
        }

        if (!cmd) {
            cmd = vk_ps4_alloc_zero(alloc, sizeof(*cmd), 16);
            if (!cmd) {
                for (uint32_t j = 0; j < i; j++) {
                    VkPs4CommandBuffer *c = (VkPs4CommandBuffer *)pCommandBuffers[j];
                    if (c) {
                        vk_ps4_release_pm4_storage(c, alloc);
                        vk_ps4_free(alloc, c);
                    }
                    pCommandBuffers[j] = VK_NULL_HANDLE;
                }
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            cmd->pm4_buffer = NULL;
            cmd->gnm_vertex_buffers = NULL;
            cmd->vertex_table_cpu_keys = NULL;
        }

        cmd->type = VK_PS4_OBJ_COMMAND_BUFFER;
        cmd->device = dev;
        cmd->pool = pool;
        cmd->level = pAllocateInfo->level;
        cmd->is_recording = false;
        cmd->is_begin = false;
        cmd->current_pipeline = NULL;
    cmd->graphics_descriptor_layout = NULL;
    memset(cmd->graphics_push_constants, 0, sizeof(cmd->graphics_push_constants));
    memset(cmd->graphics_push_valid, 0, sizeof(cmd->graphics_push_valid));
    vk_ps4_reset_user_data_state(cmd);
        cmd->vertex_binding_count = 0;

        /* Allocate PM4 buffer if not reused */
        if (!cmd->pm4_buffer) {
            cmd->pm4_buffer_size = VK_PS4_CMD_BUFFER_SIZE / sizeof(uint32_t);
            GnmError pm4_error = sceGnmDirectMemoryAllocate(
                &cmd->pm4_mem,
                VK_PS4_CMD_BUFFER_ALLOCATION_SIZE,
                64u * 1024u,
                GNM_DIRECT_MEMORY_TYPE_WC_GARLIC,
                GNM_PROT_CPU_GPU_RW
            );
            if (pm4_error == GNM_ERROR_OK) {
                cmd->pm4_buffer = (uint32_t *)cmd->pm4_mem.mapped;
                memset(cmd->pm4_buffer, 0,
                       cmd->pm4_buffer_size * sizeof(uint32_t));
            }
            if (!cmd->pm4_buffer) {
                vk_ps4_free(alloc, cmd);
                for (uint32_t j = 0; j < i; j++) {
                    VkPs4CommandBuffer *c = (VkPs4CommandBuffer *)pCommandBuffers[j];
                    if (c) {
                        vk_ps4_release_pm4_storage(c, alloc);
                        vk_ps4_free(alloc, c);
                    }
                    pCommandBuffers[j] = VK_NULL_HANDLE;
                }
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            vk_ps4_log("B20 command storage: fixed PM4=%u KiB vertex=%u KiB dynamic=%u KiB; no recording growth",
                VK_PS4_CMD_BUFFER_ALLOCATION_SIZE / 1024u,
                (unsigned)(VK_PS4_MAX_VERTEX_TABLE_SNAPSHOTS * VK_PS4_MAX_VERTEX_DESCRIPTORS * sizeof(GnmBuffer) / 1024u),
                (unsigned)(VK_PS4_MAX_DYNAMIC_TABLE_SNAPSHOTS * VK_PS4_MAX_DYNAMIC_DESCRIPTORS * sizeof(GnmBuffer) / 1024u));
        } else {
            /* Reused buffer — clear the PM4 contents */
            memset(cmd->pm4_buffer, 0, cmd->pm4_buffer_size * sizeof(uint32_t));
        }
        cmd->pm4_used = 0;

        /* The generated fetch shader dereferences this table directly.  Keep
         * it in GPU-visible direct memory for the command buffer's lifetime;
         * ordinary allocator memory is CPU-only on retail PS4. */
        if (!cmd->vertex_table_mem.allocated) {
            const uint64_t table_size =
                (uint64_t)VK_PS4_MAX_VERTEX_TABLE_SNAPSHOTS *
                VK_PS4_MAX_VERTEX_DESCRIPTORS * sizeof(GnmBuffer);
            GnmError table_error = sceGnmDirectMemoryAllocate(
                &cmd->vertex_table_mem, table_size, 64u * 1024u,
                GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW
            );
            if (table_error != GNM_ERROR_OK ||
                !cmd->vertex_table_mem.mapped) {
                vk_ps4_release_pm4_storage(cmd, alloc);
                vk_ps4_free(alloc, cmd);
                for (uint32_t j = 0; j < i; j++) {
                    VkPs4CommandBuffer *c =
                        (VkPs4CommandBuffer *)pCommandBuffers[j];
                    if (c) {
                        vk_ps4_release_pm4_storage(c, alloc);
                        vk_ps4_free(alloc, c);
                    }
                    pCommandBuffers[j] = VK_NULL_HANDLE;
                }
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
        }
        cmd->gnm_vertex_buffers =
            (GnmBuffer *)cmd->vertex_table_mem.mapped;
        if (!cmd->vertex_table_cpu_keys) {
            const size_t key_bytes =
                (size_t)VK_PS4_MAX_VERTEX_TABLE_SNAPSHOTS *
                VK_PS4_MAX_VERTEX_DESCRIPTORS * sizeof(GnmBuffer);
            cmd->vertex_table_cpu_keys =
                (GnmBuffer *)vk_ps4_alloc_zero(alloc, key_bytes, 64u);
            if (!cmd->vertex_table_cpu_keys) {
                vk_ps4_release_pm4_storage(cmd, alloc);
                vk_ps4_free(alloc, cmd);
                for (uint32_t j = 0; j < i; ++j) {
                    VkPs4CommandBuffer *c =
                        (VkPs4CommandBuffer *)pCommandBuffers[j];
                    if (c) {
                        vk_ps4_release_pm4_storage(c, alloc);
                        vk_ps4_free(alloc, c);
                    }
                    pCommandBuffers[j] = VK_NULL_HANDLE;
                }
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            vk_ps4_log("vertex cache: %u KiB CPU-cached host keys; GPU snapshots remain immutable Garlic; no GPU readback on lookup",
                (unsigned)(key_bytes / 1024u));
        }
        cmd->vertex_table_slot_cursor = 0;
        memset(cmd->vertex_table_hash, 0, sizeof(cmd->vertex_table_hash));
        cmd->vertex_table_slot_capacity =
            VK_PS4_MAX_VERTEX_TABLE_SNAPSHOTS;
        cmd->vertex_table_overflow = false;
        memset(cmd->gnm_vertex_buffers, 0,
               (size_t)VK_PS4_MAX_VERTEX_TABLE_SNAPSHOTS *
               VK_PS4_MAX_VERTEX_DESCRIPTORS * sizeof(GnmBuffer));

        if (!cmd->dynamic_table_mem.allocated) {
            const uint64_t dynamic_size =
                (uint64_t)VK_PS4_MAX_DYNAMIC_TABLE_SNAPSHOTS *
                VK_PS4_MAX_DYNAMIC_DESCRIPTORS * sizeof(GnmBuffer);
            GnmError dynamic_error = sceGnmDirectMemoryAllocate(
                &cmd->dynamic_table_mem, dynamic_size, 64u * 1024u,
                GNM_DIRECT_MEMORY_TYPE_WC_GARLIC, GNM_PROT_CPU_GPU_RW);
            if (dynamic_error != GNM_ERROR_OK ||
                !cmd->dynamic_table_mem.mapped) {
                vk_ps4_release_pm4_storage(cmd, alloc);
                vk_ps4_free(alloc, cmd);
                for (uint32_t j = 0; j < i; ++j) {
                    VkPs4CommandBuffer *c =
                        (VkPs4CommandBuffer *)pCommandBuffers[j];
                    if (c) {
                        vk_ps4_release_pm4_storage(c, alloc);
                        vk_ps4_free(alloc, c);
                    }
                    pCommandBuffers[j] = VK_NULL_HANDLE;
                }
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
        }
        cmd->dynamic_descriptor_tables =
            (GnmBuffer *)cmd->dynamic_table_mem.mapped;
        cmd->dynamic_table_cursor = 0;
        cmd->dynamic_table_overflow = false;
        memset(cmd->dynamic_descriptor_tables, 0,
               (size_t)VK_PS4_MAX_DYNAMIC_TABLE_SNAPSHOTS *
               VK_PS4_MAX_DYNAMIC_DESCRIPTORS * sizeof(GnmBuffer));
        vk_ps4_cpu_store_fence();

        /* Register in pool for cleanup. If pool is full, fail to avoid leak. */
        if (pool) {
            if (pool->command_buffer_count < VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL) {
                pool->command_buffers[pool->command_buffer_count++] = cmd;
            } else {
                vk_ps4_release_pm4_storage(cmd, alloc);
                vk_ps4_free(alloc, cmd);
                for (uint32_t j = 0; j < i; j++) {
                    VkPs4CommandBuffer *c = (VkPs4CommandBuffer *)pCommandBuffers[j];
                    if (c) {
                        for (uint32_t k = 0; k < pool->command_buffer_count; k++) {
                            if (pool->command_buffers[k] == c) {
                                pool->command_buffers[k] = NULL;
                                break;
                            }
                        }
                        vk_ps4_release_pm4_storage(c, alloc);
                        vk_ps4_free(alloc, c);
                    }
                    pCommandBuffers[j] = VK_NULL_HANDLE;
                }
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
        }

        pCommandBuffers[i] = (VkCommandBuffer)cmd;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_FreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                          uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers) {
    if (!device || !pCommandBuffers) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;

    for (uint32_t i = 0; i < commandBufferCount; i++) {
        if (!pCommandBuffers[i]) continue;
        VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)pCommandBuffers[i];

        /* Remove from pool's tracking array to avoid double-free in DestroyCommandPool */
        if (pool) {
            for (uint32_t j = 0; j < pool->command_buffer_count; j++) {
                if (pool->command_buffers[j] == cmd) {
                    pool->command_buffers[j] = NULL;
                    /* Compact: move last element into the gap */
                    if (j < pool->command_buffer_count - 1) {
                        pool->command_buffers[j] = pool->command_buffers[pool->command_buffer_count - 1];
                        pool->command_buffers[pool->command_buffer_count - 1] = NULL;
                    }
                    pool->command_buffer_count--;
                    break;
                }
            }
        }

        /* Add to the pool's free list for reuse, or free if pool is full */
        if (pool && pool->free_count < VK_PS4_MAX_COMMAND_BUFFERS_PER_POOL) {
            pool->free_list[pool->free_count++] = cmd;
        } else {
            vk_ps4_release_pm4_storage(cmd, alloc);
            vk_ps4_free(alloc, cmd);
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo) {
    (void)pBeginInfo;
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (!cmd->pm4_buffer) return VK_ERROR_INITIALIZATION_FAILED;

    /* Initialize GnmCommandBuffer with the PM4 buffer */
    cmd->recording_error = VK_SUCCESS;
    cmd->compute_dispatch_count = 0;
    cmd->pm4_segment_count = 1;
    memset(cmd->pm4_segment_used, 0, sizeof(cmd->pm4_segment_used));
    GnmCommandCallbackFunc overflow_callback = vk_ps4_command_overflow;
    cmd->gnm_cmd = sceGnmCmdInit(
        cmd->pm4_buffer, cmd->pm4_buffer_size * sizeof(uint32_t),
        &overflow_callback, cmd
    );
    // The pinned library leaves these reserved/state fields uninitialized.
    cmd->gnm_cmd.flags = (GnmCommandBufferFlags){0};
    cmd->gnm_cmd._unused = 0;
    cmd->gnm_cmd._unused2 = 0;

    /* Emit default hardware state only for primary command buffers.
     * Secondary buffers are executed within a primary's context, so they
     * inherit the default state from the primary. */
    if (cmd->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
        sceGnmDrawCmdInitDefaultHardwareState(&cmd->gnm_cmd);
        /* B21: PSBC uses RADV's GFX7 VS ABI: v1 contains
         * InstanceID / StepRate0, not the raw instance ID in v3.  Mesa's
         * graphics preamble sets this divisor to one; GNM's firmware
         * defaults are not that preamble.  Leaving the divisor inherited
         * can collapse a whole instanced prop batch onto one transform as
         * the visible-instance list changes.  Preserve compiler VGPR
         * allocation/input counts and establish the required hardware ABI
         * once per primary; secondary buffers and meta-clears inherit it.
         * Instance-rate vertex fetch uses raw v3, so StepRate1 is unused. */
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                                R_028AA0_VGT_INSTANCE_STEP_RATE_0, 1u);
    }

    if (cmd->recording_error != VK_SUCCESS) return cmd->recording_error;
    cmd->is_recording = true;
    cmd->is_begin = true;
    cmd->viewport0_valid = false;
    cmd->scissor0_valid = false;
    cmd->pm4_used = (uint32_t)(cmd->gnm_cmd.cmdptr - cmd->gnm_cmd.beginptr);
    /* Reset all tracking state */
    cmd->current_pipeline = NULL;
    cmd->graphics_descriptor_layout = NULL;
    memset(cmd->graphics_push_constants, 0, sizeof(cmd->graphics_push_constants));
    memset(cmd->graphics_push_valid, 0, sizeof(cmd->graphics_push_valid));
    vk_ps4_reset_user_data_state(cmd);
    cmd->vertex_binding_count = 0;
    cmd->vertex_buffers_dirty = false;
    cmd->gnm_vertex_buffers =
        (GnmBuffer *)cmd->vertex_table_mem.mapped;
    cmd->vertex_table_slot_cursor = 0;
    memset(cmd->vertex_table_hash, 0, sizeof(cmd->vertex_table_hash));
    cmd->vertex_table_slot_capacity =
        VK_PS4_MAX_VERTEX_TABLE_SNAPSHOTS;
    cmd->vertex_table_overflow = false;
    cmd->dynamic_descriptor_tables =
        (GnmBuffer *)cmd->dynamic_table_mem.mapped;
    cmd->dynamic_table_cursor = 0;
    cmd->dynamic_table_overflow = false;
    memset(cmd->bound_graphics_sets, 0, sizeof(cmd->bound_graphics_sets));
    memset(cmd->bound_compute_sets, 0, sizeof(cmd->bound_compute_sets));
    memset(cmd->graphics_dynamic_offsets, 0,
           sizeof(cmd->graphics_dynamic_offsets));
    memset(cmd->compute_dynamic_offsets, 0,
           sizeof(cmd->compute_dynamic_offsets));
    memset(&cmd->index_buffer, 0, sizeof(cmd->index_buffer));
    memset(cmd->vertex_buffers, 0, sizeof(cmd->vertex_buffers));
    cmd->vertex_descriptor_count = 0;
    memset(&cmd->current_render_pass, 0, sizeof(cmd->current_render_pass));
    /* Reset stencil shadow state */
    cmd->stencil_refmask_front = 0;
    cmd->stencil_refmask_back = 0;
    cmd->stencil_shadow_valid = false;

    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_EndCommandBuffer(VkCommandBuffer commandBuffer) {
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    cmd->is_recording = false;
    if (!cmd->pm4_segment_count || cmd->pm4_segment_count > VK_PS4_MAX_PM4_SEGMENTS)
        return VK_ERROR_INITIALIZATION_FAILED;
    cmd->pm4_segment_used[cmd->pm4_segment_count - 1u] =
        (uint32_t)(cmd->gnm_cmd.cmdptr - cmd->gnm_cmd.beginptr);
    cmd->pm4_used = cmd->pm4_segment_used[0];
    uint32_t total = 0;
    for (uint32_t i = 0; i < cmd->pm4_segment_count; ++i) total += cmd->pm4_segment_used[i];
    if (total >= 16u * 1024u) {
        ++cmd->pm4_world_recordings;
        const bool capacity_step = total / (32u * 1024u) > cmd->pm4_high_water / (32u * 1024u);
        if (total > cmd->pm4_high_water) cmd->pm4_high_water = total;
        if (capacity_step || cmd->pm4_world_recordings % 120u == 0u)
            vk_ps4_log("[PM4_SEGMENTS] usedKiB=%u segments=%u highWaterKiB=%u vertex=%u/%u dynamic=%u/%u result=%d",
                total / 256u, cmd->pm4_segment_count, cmd->pm4_high_water / 256u,
                cmd->vertex_table_slot_cursor, cmd->vertex_table_slot_capacity,
                cmd->dynamic_table_cursor, VK_PS4_MAX_DYNAMIC_TABLE_SNAPSHOTS, (int)cmd->recording_error);
    }
    if (cmd->recording_error != VK_SUCCESS) return cmd->recording_error;
    if (cmd->vertex_table_overflow) {
        vk_ps4_log("EndCommandBuffer: immutable vertex table arena exhausted; submit rejected");
        cmd->recording_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        return cmd->recording_error;
    }
    if (cmd->dynamic_table_overflow) {
        vk_ps4_log("EndCommandBuffer: immutable dynamic descriptor arena exhausted; submit rejected");
        cmd->recording_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
        return cmd->recording_error;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    (void)flags;
    if (!commandBuffer) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (cmd->pm4_buffer && cmd->gnm_cmd.beginptr) {
        sceGnmCmdReset(&cmd->gnm_cmd);
    }
    cmd->pm4_used = 0;
    cmd->recording_error = VK_SUCCESS;
    cmd->compute_dispatch_count = 0;
    cmd->is_recording = false;
    /* Reset all tracking state */
    cmd->current_pipeline = NULL;
    cmd->graphics_descriptor_layout = NULL;
    memset(cmd->graphics_push_constants, 0, sizeof(cmd->graphics_push_constants));
    memset(cmd->graphics_push_valid, 0, sizeof(cmd->graphics_push_valid));
    vk_ps4_reset_user_data_state(cmd);
    cmd->vertex_binding_count = 0;
    cmd->vertex_buffers_dirty = false;
    cmd->gnm_vertex_buffers =
        (GnmBuffer *)cmd->vertex_table_mem.mapped;
    cmd->vertex_table_slot_cursor = 0;
    memset(cmd->vertex_table_hash, 0, sizeof(cmd->vertex_table_hash));
    cmd->vertex_table_slot_capacity =
        VK_PS4_MAX_VERTEX_TABLE_SNAPSHOTS;
    cmd->vertex_table_overflow = false;
    cmd->dynamic_descriptor_tables =
        (GnmBuffer *)cmd->dynamic_table_mem.mapped;
    cmd->dynamic_table_cursor = 0;
    cmd->dynamic_table_overflow = false;
    memset(cmd->bound_graphics_sets, 0, sizeof(cmd->bound_graphics_sets));
    memset(cmd->bound_compute_sets, 0, sizeof(cmd->bound_compute_sets));
    memset(cmd->graphics_dynamic_offsets, 0,
           sizeof(cmd->graphics_dynamic_offsets));
    memset(cmd->compute_dynamic_offsets, 0,
           sizeof(cmd->compute_dynamic_offsets));
    memset(&cmd->index_buffer, 0, sizeof(cmd->index_buffer));
    memset(cmd->vertex_buffers, 0, sizeof(cmd->vertex_buffers));
    cmd->vertex_descriptor_count = 0;
    memset(&cmd->current_render_pass, 0, sizeof(cmd->current_render_pass));
    /* Reset stencil shadow state */
    cmd->stencil_refmask_front = 0;
    cmd->stencil_refmask_back = 0;
    cmd->stencil_shadow_valid = false;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) {
    (void)flags;
    if (!device || !commandPool) return VK_ERROR_INITIALIZATION_FAILED;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;

    /* Free all command buffers in the free list */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        if (pool->free_list[i]) {
            vk_ps4_release_pm4_storage(pool->free_list[i], alloc);
            vk_ps4_free(alloc, pool->free_list[i]);
            pool->free_list[i] = NULL;
        }
    }
    pool->free_count = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_TrimCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolTrimFlags flags) {
    (void)flags;
    if (!device || !commandPool) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = &dev->allocator;
    VkPs4CommandPool *pool = (VkPs4CommandPool *)commandPool;

    /* Free all command buffers in the free list — they're not in use */
    for (uint32_t i = 0; i < pool->free_count; i++) {
        if (pool->free_list[i]) {
            vk_ps4_release_pm4_storage(pool->free_list[i], alloc);
            vk_ps4_free(alloc, pool->free_list[i]);
            pool->free_list[i] = NULL;
        }
    }
    pool->free_count = 0;
}

/* === Command buffer recording === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    if (!commandBuffer || !pipeline) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Pipeline *pipe = (VkPs4Pipeline *)pipeline;

    /* Validate bind point matches pipeline type */
    if (pipe->bind_point != pipelineBindPoint) return;

    cmd->current_pipeline = pipe;

    /* Whatever is bound reaches the new shaders through their own user-data
     * registers: sets, push constants and the vertex table go out again
     * before the next draw or dispatch (vk_ps4_flush_graphics_user_data).
     * Emitting them only at bind time dropped every set bound before the
     * pipeline, which Vulkan allows and the character renderer does with
     * its per-frame set (B4 test 11: the first scene draw faulted reading
     * the camera UBO through an SGPR pair nothing had written). */
    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        cmd->graphics_tables_dirty = true;
        cmd->graphics_push_dirty[0] = true;
        cmd->graphics_push_dirty[1] = true;
        cmd->vertex_buffers_dirty =
            cmd->vertex_descriptor_count > 0 && cmd->gnm_vertex_buffers != NULL;
    } else {
        cmd->compute_tables_dirty = true;
        cmd->compute_push_dirty = true;
    }

    if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        vk_ps4_emit_color_control(&cmd->gnm_cmd, pipe);
        vk_ps4_emit_clip_control(&cmd->gnm_cmd, pipe);
        /* Static viewport/scissor values are part of the pipeline.  The old
         * backend shallow-copied the caller pointers and never emitted them,
         * leaving a fresh command buffer clipped to stale/default state. */
        if (!pipe->dynamic_viewport && pipe->static_viewport_count > 0) {
            vk_ps4_CmdSetViewport(commandBuffer, 0,
                                  pipe->static_viewport_count,
                                  pipe->static_viewports);
        }
        if (!pipe->dynamic_scissor && pipe->static_scissor_count > 0) {
            vk_ps4_CmdSetScissor(commandBuffer, 0,
                                 pipe->static_scissor_count,
                                 pipe->static_scissors);
        }

        /* PA_SU_SC_MODE_CNTL is not part of SetPrimitiveType.  Emit the
         * pipeline's culling, winding, fill and depth-bias enables explicitly
         * so a prior pipeline/clear cannot leak raster state into this draw. */
        GnmPrimitiveSetup primitive_setup;
        memset(&primitive_setup, 0, sizeof(primitive_setup));
        switch (pipe->rasterization_state.cullMode) {
        case VK_CULL_MODE_FRONT_BIT: primitive_setup.cullmode = GNM_CULL_FRONT; break;
        case VK_CULL_MODE_BACK_BIT: primitive_setup.cullmode = GNM_CULL_BACK; break;
        case VK_CULL_MODE_FRONT_AND_BACK: primitive_setup.cullmode = GNM_CULL_FRONTBACK; break;
        default: primitive_setup.cullmode = GNM_CULL_NONE; break;
        }
        primitive_setup.frontface =
            pipe->rasterization_state.frontFace == VK_FRONT_FACE_CLOCKWISE
                ? GNM_FACE_CW : GNM_FACE_CCW;
        GnmFillMode fill_mode = GNM_FILL_SOLID;
        if (pipe->rasterization_state.polygonMode == VK_POLYGON_MODE_LINE)
            fill_mode = GNM_FILL_WIREFRAME;
        else if (pipe->rasterization_state.polygonMode == VK_POLYGON_MODE_POINT)
            fill_mode = GNM_FILL_POINTS;
        primitive_setup.frontmode = fill_mode;
        primitive_setup.backmode = fill_mode;
        primitive_setup.frontoffsetmode =
            pipe->rasterization_state.depthBiasEnable == VK_TRUE;
        primitive_setup.backoffsetmode =
            pipe->rasterization_state.depthBiasEnable == VK_TRUE;
        primitive_setup.provokemode = GNM_PROVOKINGVTX_FIRST;
        primitive_setup.perspectivecorrectiondisable = false;
        sceGnmDrawCmdSetPrimitiveSetup(&cmd->gnm_cmd, &primitive_setup);
        if (pipe->rasterization_state.depthBiasEnable &&
            !pipe->dynamic_depth_bias) {
            vk_ps4_CmdSetDepthBias(
                commandBuffer,
                pipe->rasterization_state.depthBiasConstantFactor,
                pipe->rasterization_state.depthBiasClamp,
                pipe->rasterization_state.depthBiasSlopeFactor
            );
        }
        if (!pipe->dynamic_line_width) {
            vk_ps4_CmdSetLineWidth(commandBuffer,
                                  pipe->rasterization_state.lineWidth > 0.0f
                                      ? pipe->rasterization_state.lineWidth : 1.0f);
        }

        /* Set primitive type */
        sceGnmDrawCmdSetPrimitiveType(&cmd->gnm_cmd,
            vk_topology_to_gnm(pipe->input_assembly_state.topology));

        /* Set vertex shader (or LS/ES if tessellation/geometry is active).
         * On GCN, the post-tessellation vertex stage (domain shader / TES
         * compiled as DS_VS) runs on the VS hardware, so SetVsShader must
         * be called for the TES registers when tessellation is active. */
        if (pipe->has_ls) {
            /* Tessellation path: VS is compiled as LS (local shader).
             * SetLsShader sets the LS stage (pre-tessellation vertex). */
            sceGnmDrawCmdSetLsShader(&cmd->gnm_cmd, &pipe->ls_regs, 0);
            /* The TES (domain shader) runs on VS hardware post-tessellation.
             * Emit SetVsShader with the TES registers (stored in vs_regs). */
            if (pipe->has_ds_vs) {
                sceGnmDrawCmdSetVsShader(&cmd->gnm_cmd, &pipe->vs_regs, 0);
            }
        } else if (pipe->has_es && pipe->has_gs) {
            /* GS-only pipeline (no tess): VS compiled as ES.
             * ES is set in the has_gs block below — don't call SetVsShader
             * here because vs_regs is zeroed (VS was compiled as ES). */
        } else if (pipe->has_es && !pipe->has_gs) {
            /* TES compiled as ES (DS_ES path, no GS) */
            sceGnmDrawCmdSetEsShader(&cmd->gnm_cmd, &pipe->es_regs, 0);
        } else {
            /* Standard VS or DS_VS (no tess) */
            sceGnmDrawCmdSetVsShader(&cmd->gnm_cmd, &pipe->vs_regs, 0);
        }

        /* Set hull shader (tessellation control) if present */
        if (pipe->has_hs) {
            /* lshsconfig = VGT_LS_HS_CONFIG register value.
             * HS_NUM_INPUT_CP = patchControlPoints (from Vulkan tess state)
             * HS_NUM_OUTPUT_CP = patchControlPoints (default: same as input
             *   when TCS output CP count is unknown — the shader compiler
             *   may override this in the shader binary) */
            uint32_t cp = pipe->tess_patch_control_points;
            if (cp > 32) cp = 32;  /* 6-bit field, max 32 */
            if (cp == 0) cp = 3;   /* default patch size */
            uint32_t lshsconfig = S_028B58_HS_NUM_INPUT_CP(cp) |
                                   S_028B58_HS_NUM_OUTPUT_CP(cp);
            sceGnmDrawCmdSetHsShader(&cmd->gnm_cmd, &pipe->hs_regs, lshsconfig);
        }

        /* Set geometry shader if present */
        if (pipe->has_gs) {
            sceGnmDrawCmdSetGsShader(&cmd->gnm_cmd, &pipe->gs_regs);
            /* If VS was compiled as ES (for GS path), set ES shader */
            if (pipe->has_es) {
                sceGnmDrawCmdSetEsShader(&cmd->gnm_cmd, &pipe->es_regs, 0);
            }
        }

        /* Set pixel shader only if the pipeline has one */
        if (pipe->has_ps) {
            sceGnmDrawCmdSetPsShader(&cmd->gnm_cmd, &pipe->ps_regs);
            /* Program SPI_PS_INPUT_CNTL_n from the compiler's semantic
             * tables.  PS stage registers only describe how many inputs are
             * consumed; they do not map those inputs to VS export slots. */
            vk_ps4_emit_ps_input_linkage(
                &cmd->gnm_cmd,
                pipe->vs_export_semantics,
                pipe->vs_export_semantic_count,
                pipe->ps_input_semantics,
                pipe->ps_input_semantic_count
            );
        }

        if (!pipe->has_ps) {
            vk_ps4_emit_ps_input_linkage(&cmd->gnm_cmd, NULL, 0, NULL, 0);
            sceGnmDrawCmdSetEmbeddedPsShader(&cmd->gnm_cmd, GNM_EMBEDDED_PSH_DUMMY);
        }

        /* Bind fetch shader if the pipeline has one */
        if (pipe->has_fetch_shader && pipe->has_fetch_shader_slot && pipe->fetch_shader) {
            vk_ps4_cpu_store_fence();
            sceGnmDrawCmdSetPointerUserData(
                &cmd->gnm_cmd, GNM_STAGE_VS, pipe->fetch_shader_slot,
                pipe->fetch_shader
            );
        }

        /* A pipeline can be bound after vertex buffers.  Rebuild now so the
         * table uses this pipeline's attribute formats and semantic order. */
        if (cmd->vertex_table_pipeline != pipe)
            vk_ps4_rebuild_vertex_table(cmd);

        /* Emit blend state + render target mask.
         * This must be done every time a pipeline is bound, because
         * draw-based clears (vk_ps4_clear_color_draw) clobber blend state
         * and RT mask. Without this, the clear's blend state would persist. */
        if (pipe->has_blend_state) {
            /* Set blend constants */
            sceGnmDrawCmdSetBlendColor(&cmd->gnm_cmd,
                pipe->blend_constants[0], pipe->blend_constants[1],
                pipe->blend_constants[2], pipe->blend_constants[3]);

            /* Emit blend control for each RT slot */
            for (uint32_t j = 0; j < pipe->blend_control_count && j < 8; j++) {
                sceGnmDrawCmdSetBlendControl(&cmd->gnm_cmd, j,
                    &pipe->blend_controls[j]);
            }

            /* Set render target mask from colorWriteMask */
            sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd,
                pipe->color_write_mask);
        } else {
            /* No blend state — disable blending for all RTs, write all channels */
            GnmBlendControl no_blend;
            memset(&no_blend, 0, sizeof(no_blend));
            no_blend.blendenabled = false;
            for (uint32_t j = 0; j < 8; j++) {
                sceGnmDrawCmdSetBlendControl(&cmd->gnm_cmd, j, &no_blend);
            }
            sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd, 0xFFFFFFFF);
        }

        if (!pipe->has_ps)
            sceGnmDrawCmdSetRenderTargetMask(&cmd->gnm_cmd, 0);

        /* Emit depth/stencil state.
         * When pDepthStencilState is NULL, the Vulkan spec requires that
         * no depth/stencil operations are performed. We must explicitly
         * disable depth/stencil to prevent inheriting state from a
         * previous pipeline. */
        if (pipe->has_depth_stencil_state) {
            /* DB_DEPTH_CONTROL: depth/stencil enable, compare funcs */
            sceGnmDrawCmdSetDepthStencilControl(&cmd->gnm_cmd, &pipe->depth_stencil_control);

            /* Stencil ops, ref/mask (emitted via direct PM4 since GNM API
             * doesn't have wrappers for these registers) */
            if (pipe->depth_stencil_control.stencilenable) {
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                    R_02842C_DB_STENCIL_CONTROL, pipe->stencil_control);
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                    R_028430_DB_STENCILREFMASK, pipe->stencil_refmask);
                if (pipe->depth_stencil_control.separatestencilenable) {
                    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                        R_028434_DB_STENCILREFMASK_BF, pipe->stencil_refmask_bf);
                }
                /* Initialize shadow state from pipeline so dynamic
                 * stencil commands can do read-modify-write. */
                cmd->stencil_refmask_front = pipe->stencil_refmask;
                cmd->stencil_refmask_back = pipe->stencil_refmask_bf;
                cmd->stencil_shadow_valid = true;
            } else {
                /* Stencil disabled — invalidate shadow state so
                 * CmdSetStencil* won't emit stale values. */
                cmd->stencil_shadow_valid = false;
            }

            /* Depth bounds test */
            if (pipe->depth_stencil_control.depthboundsenable) {
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                    R_028020_DB_DEPTH_BOUNDS_MIN,
                    vk_ps4_fui(pipe->depth_stencil_state.minDepthBounds));
                vk_ps4_emit_context_reg(&cmd->gnm_cmd,
                    R_028024_DB_DEPTH_BOUNDS_MAX,
                    vk_ps4_fui(pipe->depth_stencil_state.maxDepthBounds));
            }
        } else {
            /* No depth/stencil state — explicitly disable all DS operations */
            GnmDepthStencilControl ds_off;
            memset(&ds_off, 0, sizeof(ds_off));
            ds_off.depthenable = 0;
            ds_off.zwrite = 0;
            ds_off.stencilenable = 0;
            ds_off.depthboundsenable = 0;
            sceGnmDrawCmdSetDepthStencilControl(&cmd->gnm_cmd, &ds_off);
        }
    } else if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
        sceGnmDrawCmdSetCsShader(&cmd->gnm_cmd, &pipe->cs_regs);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports) {
    if (!commandBuffer || !pViewports) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* GNM viewport: scale/offset maps Vulkan viewport to GNM */
    for (uint32_t i = 0; i < viewportCount; i++) {
        const VkViewport *vp = &pViewports[i];
        if (firstViewport + i == 0) {
            cmd->viewport0 = *vp;
            cmd->viewport0_valid = true;
        }
        GnmSetViewportInfo vp_info;
        vp_info.dmin = vp->minDepth;
        vp_info.dmax = vp->maxDepth;
        /* Vulkan viewport: x, y is top-left, width/height extend right/down
         * GNM viewport: scale = half-dimension, offset = center */
        vp_info.scale[0] = vp->width * 0.5f;
        vp_info.scale[1] = vp->height * 0.5f;
        /* Vulkan NDC depth is [0,1], not OpenGL's [-1,1]. */
        vp_info.scale[2] = vp->maxDepth - vp->minDepth;
        vp_info.offset[0] = vp->x + vp->width * 0.5f;
        vp_info.offset[1] = vp->y + vp->height * 0.5f;
        vp_info.offset[2] = vp->minDepth;
        sceGnmDrawCmdSetViewport(&cmd->gnm_cmd, firstViewport + i, &vp_info);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors) {
    if (!commandBuffer || !pScissors) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    for (uint32_t i = 0; i < scissorCount; i++) {
        const VkRect2D *sc = &pScissors[i];
        if (firstScissor + i == 0) {
            cmd->scissor0 = *sc;
            cmd->scissor0_valid = true;
        }
        sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd,
            sc->offset.x, sc->offset.y,
            sc->offset.x + sc->extent.width,
            sc->offset.y + sc->extent.height);
    }
}

/* === Dynamic state commands (Phase 3 Step 30) === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) {
    if (!commandBuffer || !blendConstants) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    sceGnmDrawCmdSetBlendColor(&cmd->gnm_cmd,
        blendConstants[0], blendConstants[1],
        blendConstants[2], blendConstants[3]);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor,
                       float depthBiasClamp, float depthBiasSlopeFactor) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* GCN polygon offset: scale and offset are in fixed-point format.
     * The hardware expects the float values converted to uint32 bit patterns
     * (the GPU interprets them as floats). */
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE,
        vk_ps4_fui(depthBiasSlopeFactor));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET,
        vk_ps4_fui(depthBiasConstantFactor));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE,
        vk_ps4_fui(depthBiasSlopeFactor));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET,
        vk_ps4_fui(depthBiasConstantFactor));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028B7C_PA_SU_POLY_OFFSET_CLAMP,
        vk_ps4_fui(depthBiasClamp));
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028020_DB_DEPTH_BOUNDS_MIN, vk_ps4_fui(minDepthBounds));
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028024_DB_DEPTH_BOUNDS_MAX, vk_ps4_fui(maxDepthBounds));
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                                 uint32_t compareMask) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* DB_STENCILREFMASK: [7:0]=TESTVAL, [15:8]=MASK, [23:16]=WRITEMASK, [31:24]=OPVAL
     * Read-modify-write: only update the MASK field, preserving the others.
     * If no pipeline with stencil enabled has been bound, skip — emitting
     * stale register values would be a spec violation. */
    if (!cmd->stencil_shadow_valid) return;
    if (faceMask & VK_STENCIL_FACE_FRONT_BIT) {
        cmd->stencil_refmask_front = (cmd->stencil_refmask_front & C_028430_STENCILMASK) |
                                      S_028430_STENCILMASK(compareMask & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028430_DB_STENCILREFMASK, cmd->stencil_refmask_front);
    }
    if (faceMask & VK_STENCIL_FACE_BACK_BIT) {
        cmd->stencil_refmask_back = (cmd->stencil_refmask_back & C_028434_STENCILMASK_BF) |
                                     S_028434_STENCILMASK_BF(compareMask & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028434_DB_STENCILREFMASK_BF, cmd->stencil_refmask_back);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                               uint32_t writeMask) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (!cmd->stencil_shadow_valid) return;

    if (faceMask & VK_STENCIL_FACE_FRONT_BIT) {
        cmd->stencil_refmask_front = (cmd->stencil_refmask_front & C_028430_STENCILWRITEMASK) |
                                      S_028430_STENCILWRITEMASK(writeMask & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028430_DB_STENCILREFMASK, cmd->stencil_refmask_front);
    }
    if (faceMask & VK_STENCIL_FACE_BACK_BIT) {
        cmd->stencil_refmask_back = (cmd->stencil_refmask_back & C_028434_STENCILWRITEMASK_BF) |
                                     S_028434_STENCILWRITEMASK_BF(writeMask & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028434_DB_STENCILREFMASK_BF, cmd->stencil_refmask_back);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetStencilReference(VkCommandBuffer commandBuffer, VkStencilFaceFlags faceMask,
                               uint32_t reference) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (!cmd->stencil_shadow_valid) return;

    if (faceMask & VK_STENCIL_FACE_FRONT_BIT) {
        cmd->stencil_refmask_front = (cmd->stencil_refmask_front & C_028430_STENCILTESTVAL) |
                                      S_028430_STENCILTESTVAL(reference & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028430_DB_STENCILREFMASK, cmd->stencil_refmask_front);
    }
    if (faceMask & VK_STENCIL_FACE_BACK_BIT) {
        cmd->stencil_refmask_back = (cmd->stencil_refmask_back & C_028434_STENCILTESTVAL_BF) |
                                     S_028434_STENCILTESTVAL_BF(reference & 0xFF);
        vk_ps4_emit_context_reg(&cmd->gnm_cmd,
            R_028434_DB_STENCILREFMASK_BF, cmd->stencil_refmask_back);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* PA_SU_LINE_CNTL: [15:0]=WIDTH (in 4.12 fixed-point format).
     * Convert float pixels to fixed-point: width * 4096.
     * Clamp to valid range. 0xFFFF/4096 ≈ 15.9998 is the max representable
     * value in 4.12 fixed point — 16.0 would be 0x10000 which wraps to 0. */
    if (lineWidth < 0.0f) lineWidth = 0.0f;
    uint32_t width_fixed;
    if (lineWidth >= 16.0f) {
        width_fixed = 0xFFFF;  /* saturate at max representable width */
    } else {
        width_fixed = (uint32_t)(lineWidth * 4096.0f) & 0xFFFF;
    }
    vk_ps4_emit_context_reg(&cmd->gnm_cmd,
        R_028A08_PA_SU_LINE_CNTL, S_028A08_WIDTH(width_fixed));
}

/* CmdBindDescriptorSets moved to vk_ps4_descriptor.c */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                             uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets) {
    if (!commandBuffer || !pBuffers) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    bool changed = false;
    for (uint32_t i = 0; i < bindingCount; i++) {
        uint32_t idx = firstBinding + i;
        if (idx < VK_PS4_MAX_VERTEX_BINDINGS) {
            changed |= cmd->vertex_buffers[idx].buffer != pBuffers[i] ||
                       cmd->vertex_buffers[idx].offset != (pOffsets ? pOffsets[i] : 0);
            cmd->vertex_buffers[idx].buffer = pBuffers[i];
            cmd->vertex_buffers[idx].offset = pOffsets ? pOffsets[i] : 0;
        }
    }
    /* Update vertex_binding_count only for bindings that were actually stored.
     * This avoids inflating the count when firstBinding >= MAX. */
    uint32_t max_idx_stored = cmd->vertex_binding_count;
    for (uint32_t i = 0; i < bindingCount; i++) {
        uint32_t idx = firstBinding + i;
        if (idx < VK_PS4_MAX_VERTEX_BINDINGS && idx + 1 > max_idx_stored) {
            max_idx_stored = idx + 1;
        }
    }
    cmd->vertex_binding_count = max_idx_stored;
    /* Repeated material ranges often bind the same VB and offset. Its
     * immutable snapshot remains valid until a binding or pipeline changes;
     * do not consume another arena slot for the identical table. */
    if (changed || cmd->vertex_table_pipeline != cmd->current_pipeline)
        vk_ps4_rebuild_vertex_table(cmd);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (indexType != VK_INDEX_TYPE_UINT16 && indexType != VK_INDEX_TYPE_UINT32) {
        memset(&cmd->index_buffer, 0, sizeof(cmd->index_buffer));
        return;
    }
    cmd->index_buffer.buffer = buffer;
    cmd->index_buffer.offset = offset;
    cmd->index_buffer.type = indexType;

    /* Set index buffer in GNM */
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    const uint32_t index_size = indexType == VK_INDEX_TYPE_UINT32 ? 4u : 2u;
    if (buf && buf->memory && buf->memory->gnm_mem.mapped &&
        offset < buf->create_info.size && (offset % index_size) == 0) {
        void *gpu_addr = (char *)buf->memory->gnm_mem.mapped + buf->memory_offset + offset;
        sceGnmDrawCmdSetIndexBuffer(&cmd->gnm_cmd, gpu_addr);

        GnmIndexSize idx_size;
        switch (indexType) {
        case VK_INDEX_TYPE_UINT16: idx_size = GNM_INDEX_16; break;
        case VK_INDEX_TYPE_UINT32: idx_size = GNM_INDEX_32; break;
        default: idx_size = GNM_INDEX_16; break;
        }
        sceGnmDrawCmdSetIndexSize(&cmd->gnm_cmd, idx_size, GNM_POLICY_BYPASS);
        /* No INDEX_BUFFER_SIZE here. 01.16 emitted one per index-buffer
         * bind (GNM's setIndexCount) and the scene draw that had completed
         * on the console in 01.15 faulted in every launch of 01.16, with
         * the same buffers at the same addresses; the offset draws carry
         * their own bound and need no CP-side size. */
    } else {
        memset(&cmd->index_buffer, 0, sizeof(cmd->index_buffer));
    }
}

/* Push constants are emitted via SET_SH_REG PM4 packets to write
 * raw uint32_t values to the shader's user-data registers.
 * The pipeline's push_const_slots table (populated from
 * IMM_ALUFLOATCONST input usage slots) maps each push constant
 * dword index to a user-data register.
 * Optimization: consecutive user-data registers are batched into
 * a single SET_SH_REG packet to reduce PM4 overhead.
 * Uses `values`, `start_dword` and `end_dword` from the calling scope. */
#define EMIT_PUSH_CONST_BATCH(gnm_cmd, reg_base, is_cs, slots, nslots) do { \
    uint32_t _buf[VK_PS4_MAX_PUSH_CONST_DWORDS]; \
    uint32_t _n = 0; \
    uint32_t _first_reg = 0; \
    for (uint32_t _i = 0; _i < (nslots); _i++) { \
        uint32_t _dw = (slots)[_i].dword_index; \
        if (_dw < start_dword || _dw >= end_dword) { \
            if (_n > 0) { \
                uint32_t _need = 2 + _n; \
                if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) < _need) { \
                    if ((gnm_cmd)->callback.func) \
                        (gnm_cmd)->callback.func(gnm_cmd, _need, (gnm_cmd)->callback.userdata); \
                } \
                if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) >= _need) { \
                    (gnm_cmd)->cmdptr[0] = PKT3(PKT3_SET_SH_REG, _n, 0) | \
                        ((is_cs) ? PKT3_SHADER_TYPE_S(1) : 0); \
                    (gnm_cmd)->cmdptr[1] = ((reg_base) + _first_reg * 4 - SI_SH_REG_OFFSET) >> 2; \
                    for (uint32_t _j = 0; _j < _n; _j++) \
                        (gnm_cmd)->cmdptr[2 + _j] = _buf[_j]; \
                    (gnm_cmd)->cmdptr += 2 + _n; \
                } \
                _n = 0; \
            } \
            continue; \
        } \
        uint32_t _reg = (slots)[_i].user_data_reg; \
        uint32_t _val = values[_dw - start_dword]; \
        if (_n == 0) { \
            _first_reg = _reg; \
            _buf[_n++] = _val; \
        } else if (_reg == _first_reg + _n) { \
            _buf[_n++] = _val; \
        } else { \
            uint32_t _need = 2 + _n; \
            if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) < _need) { \
                if ((gnm_cmd)->callback.func) \
                    (gnm_cmd)->callback.func(gnm_cmd, _need, (gnm_cmd)->callback.userdata); \
            } \
            if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) >= _need) { \
                (gnm_cmd)->cmdptr[0] = PKT3(PKT3_SET_SH_REG, _n, 0) | \
                    ((is_cs) ? PKT3_SHADER_TYPE_S(1) : 0); \
                (gnm_cmd)->cmdptr[1] = ((reg_base) + _first_reg * 4 - SI_SH_REG_OFFSET) >> 2; \
                for (uint32_t _j = 0; _j < _n; _j++) \
                    (gnm_cmd)->cmdptr[2 + _j] = _buf[_j]; \
                (gnm_cmd)->cmdptr += 2 + _n; \
            } \
            _first_reg = _reg; \
            _n = 0; \
            _buf[_n++] = _val; \
        } \
    } \
    if (_n > 0) { \
        uint32_t _need = 2 + _n; \
        if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) < _need) { \
            if ((gnm_cmd)->callback.func) \
                (gnm_cmd)->callback.func(gnm_cmd, _need, (gnm_cmd)->callback.userdata); \
        } \
        if ((uint32_t)((gnm_cmd)->endptr - (gnm_cmd)->cmdptr) >= _need) { \
            (gnm_cmd)->cmdptr[0] = PKT3(PKT3_SET_SH_REG, _n, 0) | \
                ((is_cs) ? PKT3_SHADER_TYPE_S(1) : 0); \
            (gnm_cmd)->cmdptr[1] = ((reg_base) + _first_reg * 4 - SI_SH_REG_OFFSET) >> 2; \
            for (uint32_t _j = 0; _j < _n; _j++) \
                (gnm_cmd)->cmdptr[2 + _j] = _buf[_j]; \
            (gnm_cmd)->cmdptr += 2 + _n; \
        } \
    } \
} while (0)


/* The pushed values go to the registers of whichever pipeline the draw or
 * dispatch runs, once per pipeline bind or push (see
 * VkPs4CommandBuffer::graphics_push_dirty). Every dword the pipeline has a
 * slot for is written: user-data registers persist across shader changes,
 * and a stage that was never pushed in this command buffer reads zeros
 * rather than whatever the previous shader left. */
void vk_ps4_emit_address32_user_data(VkPs4CommandBuffer *cmd, GnmShaderStage stage,
                                   uint32_t reg, const void *address) {
    if (!cmd || cmd->recording_error != VK_SUCCESS) return;
    if (!address || ((uint64_t)(uintptr_t)address >> 32u) !=
                        VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI || reg >= 16u) {
        vk_ps4_command_fail(cmd, "PSBC compact pointer outside compiled address/register window");
        return;
    }
    if (cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr < 3) {
        if (!vk_ps4_command_overflow(&cmd->gnm_cmd, 3, cmd)) return;
    }
    const uint32_t base = stage == GNM_STAGE_CS ? R_00B900_COMPUTE_USER_DATA_0 :
        stage == GNM_STAGE_PS ? R_00B030_SPI_SHADER_USER_DATA_PS_0 :
                               R_00B130_SPI_SHADER_USER_DATA_VS_0;
    uint32_t *packet = cmd->gnm_cmd.cmdptr;
    packet[0] = PKT3(PKT3_SET_SH_REG, 1, 0) |
        (stage == GNM_STAGE_CS ? PKT3_SHADER_TYPE_S(1) : 0u);
    packet[1] = (base + reg * 4u - SI_SH_REG_OFFSET) >> 2u;
    packet[2] = (uint32_t)(uintptr_t)address;
    cmd->gnm_cmd.cmdptr += 3;
}

static void vk_ps4_flush_push_pointer(VkPs4CommandBuffer *cmd, GnmShaderStage stage,
                                     const GnmInputUsageSlot *slots, uint32_t slot_count,
                                     const uint32_t *values) {
    for (uint32_t i = 0; i < slot_count; ++i) {
        if (slots[i].usagetype != GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE ||
            slots[i].apislot != VK_PS4_PSBC_PUSH_CONSTANT_API_SLOT)
            continue;
        /* RADV spills a mat4 to a one-SGPR raw pointer when it does not fit
         * inline. B4 metadata omitted this argument completely, and the
         * character VS dereferenced unwritten s10. Store an immutable copy
         * inside this command buffer's Garlic storage: a later model push
         * must not modify any earlier draw's transform. AllocInside places
         * the data behind a PM4 NOP, so the CP never interprets it as code. */
        uint32_t *snapshot = sceGnmCmdAllocInside(&cmd->gnm_cmd,
            VK_PS4_MAX_PUSH_CONST_DWORDS * sizeof(uint32_t), 16);
        if (!snapshot) {
            vk_ps4_command_fail(cmd, "push constant snapshot allocation failed");
            return;
        }
        memcpy(snapshot, values, VK_PS4_MAX_PUSH_CONST_DWORDS * sizeof(uint32_t));
        vk_ps4_cpu_store_fence();
        vk_ps4_emit_address32_user_data(cmd, stage, slots[i].startregister, snapshot);
    }
}

static void vk_ps4_flush_push_constants(VkPs4CommandBuffer *cmd) {
    VkPs4Pipeline *pipe = cmd->current_pipeline;
    if (!pipe) return;
    const uint32_t start_dword = 0;
    const uint32_t end_dword = VK_PS4_MAX_PUSH_CONST_DWORDS;
    if (pipe->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        if (cmd->graphics_push_dirty[0]) {
            cmd->graphics_push_dirty[0] = false;
            const uint32_t *values = cmd->graphics_push_constants[0];
            vk_ps4_flush_push_pointer(cmd, GNM_STAGE_VS, pipe->vs_input_usage_slots,
                                     pipe->vs_input_usage_slot_count, values);
            if (pipe->vs_push_const_slot_count > 0) {
                EMIT_PUSH_CONST_BATCH(&cmd->gnm_cmd,
                    R_00B130_SPI_SHADER_USER_DATA_VS_0, false,
                    pipe->vs_push_const_slots, pipe->vs_push_const_slot_count);
            }
        }
        if (cmd->graphics_push_dirty[1]) {
            cmd->graphics_push_dirty[1] = false;
            const uint32_t *values = cmd->graphics_push_constants[1];
            vk_ps4_flush_push_pointer(cmd, GNM_STAGE_PS, pipe->ps_input_usage_slots,
                                     pipe->ps_input_usage_slot_count, values);
            if (pipe->ps_push_const_slot_count > 0) {
                EMIT_PUSH_CONST_BATCH(&cmd->gnm_cmd,
                    R_00B030_SPI_SHADER_USER_DATA_PS_0, false,
                    pipe->ps_push_const_slots, pipe->ps_push_const_slot_count);
            }
        }
    } else if (cmd->compute_push_dirty) {
        cmd->compute_push_dirty = false;
        const uint32_t *values = cmd->compute_push_constants;
        vk_ps4_flush_push_pointer(cmd, GNM_STAGE_CS, pipe->vs_input_usage_slots,
                                 pipe->vs_input_usage_slot_count, values);
        if (pipe->cs_push_const_slot_count > 0) {
            EMIT_PUSH_CONST_BATCH(&cmd->gnm_cmd,
                R_00B900_COMPUTE_USER_DATA_0, true,
                pipe->cs_push_const_slots, pipe->cs_push_const_slot_count);
        }
    }
}

/* Everything a graphics draw reads through user-data registers, written
 * right before it: the vertex table (rebuilt if it was built for another
 * pipeline), the descriptor set tables and the push constants. */
static void vk_ps4_flush_graphics_user_data(VkPs4CommandBuffer *cmd) {
    if (cmd->vertex_binding_count > 0 && cmd->current_pipeline &&
        cmd->vertex_table_pipeline != cmd->current_pipeline)
        vk_ps4_rebuild_vertex_table(cmd);
    vk_ps4_flush_descriptor_tables(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
    vk_ps4_flush_push_constants(cmd);
}

static void vk_ps4_flush_compute_user_data(VkPs4CommandBuffer *cmd) {
    vk_ps4_flush_descriptor_tables(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
    vk_ps4_flush_push_constants(cmd);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount,
               uint32_t firstVertex, uint32_t firstInstance) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Pipeline *bound_pipe = cmd->current_pipeline;
    static bool first_vertex_draw_logged = false;
    if (!first_vertex_draw_logged && bound_pipe) {
        vk_ps4_log("CmdDraw visibility: vertices=%u semantics=%u descriptors=%u fetch=%u fetch-slot=%u vb-table=%u fetch-bytes=%u table-slot=%u vs-exports=%u ps-inputs=%u",
                   vertexCount,
                   bound_pipe->vs_input_semantic_count,
                   cmd->vertex_descriptor_count,
                   bound_pipe->has_fetch_shader ? 1u : 0u,
                   bound_pipe->has_fetch_shader_slot ? 1u : 0u,
                   bound_pipe->has_vb_table_slot ? 1u : 0u,
                   (unsigned)bound_pipe->fetch_shader_size,
                   bound_pipe->vertex_buffer_table_slot,
                   bound_pipe->vs_export_semantic_count,
                   bound_pipe->ps_input_semantic_count);
        first_vertex_draw_logged = true;
    }
    if (!bound_pipe || bound_pipe->rasterization_state.rasterizerDiscardEnable)
        return;
    vk_ps4_flush_graphics_user_data(cmd);
    if (cmd->recording_error != VK_SUCCESS) return;
    if (bound_pipe->vertex_input_state.vertexAttributeDescriptionCount > 0 &&
        (!bound_pipe->has_fetch_shader ||
         cmd->vertex_descriptor_count != bound_pipe->vs_input_semantic_count))
        return;

    /* Emit vertex buffer table if dirty and pipeline has a VB table slot */
    if (cmd->vertex_buffers_dirty && cmd->current_pipeline &&
        cmd->current_pipeline->has_fetch_shader &&
        cmd->current_pipeline->has_vb_table_slot &&
        cmd->vertex_descriptor_count > 0 && cmd->gnm_vertex_buffers) {
        vk_ps4_cpu_store_fence();
        sceGnmDrawCmdSetPointerUserData(
            &cmd->gnm_cmd, GNM_STAGE_VS,
            cmd->current_pipeline->vertex_buffer_table_slot,
            cmd->gnm_vertex_buffers
        );
        cmd->vertex_buffers_dirty = false;
    }

    /* Always set instance count to avoid state leak from previous draw */
    sceGnmDrawCmdSetNumInstances(&cmd->gnm_cmd, instanceCount);

    /* Emit firstVertex and firstInstance via SET_SH_REG to the
     * user-data registers that psbc reserved for base_vertex and
     * start_instance. The shader adds these to gl_VertexIndex and
     * gl_InstanceIndex respectively.
     * GCN user-data registers are sticky (persist across draws), so
     * we must always write them when the pipeline uses them — even
     * when the value is 0 — to avoid stale state from a previous draw.
     * Optimization: if both registers are consecutive, emit a single
     * SET_SH_REG packet with count=2 instead of two separate packets. */
    /* The vertex offset goes to the VGT, not only to the shader. The
     * fetch shader GNM generates reads attributes at the hardware vertex
     * id; a base vertex that only reaches a user-data SGPR moves
     * gl_VertexIndex and leaves every attribute fetch at the wrong vertex.
     * VGT_INDX_OFFSET is added by the VGT to every index (auto-generated
     * ones included), so the id the fetch shader sees already carries it -
     * which is why the SGPR below is written as 0 rather than the offset.
     * Written on every draw, offset or not: the register is sticky. */
    vk_ps4_emit_context_reg(&cmd->gnm_cmd, R_028408_VGT_INDX_OFFSET, firstVertex);
    if (cmd->current_pipeline) {
        VkPs4Pipeline *pipe = cmd->current_pipeline;
        bool both = pipe->has_base_vertex_reg && pipe->has_start_instance_reg &&
                    pipe->vs_base_vertex_reg + 1 == pipe->vs_start_instance_reg;
        if (both) {
            /* Batched: single SET_SH_REG with 2 values */
            uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                pipe->vs_base_vertex_reg * 4;
            if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) < 4u &&
                    !vk_ps4_command_overflow(&cmd->gnm_cmd, 4u, cmd)) return;
                {
                cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 2, 0);
                cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                cmd->gnm_cmd.cmdptr[2] = 0u; /* carried by VGT_INDX_OFFSET */
                cmd->gnm_cmd.cmdptr[3] = firstInstance;
                cmd->gnm_cmd.cmdptr += 4;
            }
        } else {
            if (pipe->has_base_vertex_reg) {
                uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                    pipe->vs_base_vertex_reg * 4;
                if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) < 3u &&
                    !vk_ps4_command_overflow(&cmd->gnm_cmd, 3u, cmd)) return;
                {
                    cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 1, 0);
                    cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                    cmd->gnm_cmd.cmdptr[2] = 0u; /* carried by VGT_INDX_OFFSET */
                    cmd->gnm_cmd.cmdptr += 3;
                }
            }
            if (pipe->has_start_instance_reg) {
                uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                    pipe->vs_start_instance_reg * 4;
                if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) < 3u &&
                    !vk_ps4_command_overflow(&cmd->gnm_cmd, 3u, cmd)) return;
                {
                    cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 1, 0);
                    cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                    cmd->gnm_cmd.cmdptr[2] = firstInstance;
                    cmd->gnm_cmd.cmdptr += 3;
                }
            }
        }
    }

    GnmDrawModifier mod = {0};
    sceGnmDrawCmdDrawIndexAuto2(&cmd->gnm_cmd, vertexCount, mod);
}

/* What the first draws of a session actually read: every descriptor in
 * every bound set, decoded enough to compare a draw that completes with one
 * that faults (B4 test 12: the scene's opaque batch completed, the first
 * blended one died, same vertex buffer and shaders). */
static void vk_ps4_log_draw_resources(VkPs4CommandBuffer *cmd, unsigned draw_index) {
    const VkPs4Pipeline *pipe = cmd->current_pipeline;
    if (pipe) {
        const GnmBlendControl *b0 = &pipe->blend_controls[0];
        vk_ps4_log("draw[%u] pipeline: blend0=%u(src %u dst %u op %u) rtmask=0x%x zenable=%u zwrite=%u zfunc=%u a2c=%u cull=%u fetch=%p vs_code=%p ps_code=%p",
                   draw_index, pipe->has_blend_state ? (unsigned)b0->blendenabled : 0u,
                   (unsigned)b0->colorsrcmult, (unsigned)b0->colordstmult, (unsigned)b0->colorfunc,
                   pipe->has_blend_state ? pipe->color_write_mask : 0xffffffffu,
                   (unsigned)pipe->depth_stencil_control.depthenable,
                   (unsigned)pipe->depth_stencil_control.zwrite,
                   (unsigned)pipe->depth_stencil_control.zfunc,
                   (unsigned)pipe->multisample_state.alphaToCoverageEnable,
                   (unsigned)pipe->rasterization_state.cullMode,
                   pipe->fetch_shader,
                   (const void *)(uintptr_t)((uint64_t)pipe->vs_regs.spishaderpgmlovs << 8),
                   (const void *)(uintptr_t)((uint64_t)pipe->ps_regs.spishaderpgmlops << 8));
    }
    for (unsigned si = 0; si < VK_PS4_MAX_DESCRIPTOR_SETS; ++si) {
        VkPs4DescriptorSet *set = cmd->bound_graphics_sets[si];
        if (!set) continue;
        for (uint32_t bi = 0; bi < set->binding_count; ++bi) {
            const VkPs4DescriptorBinding *b = &set->bindings[bi];
            if (!b->table_base || b->count == 0) continue;
            const uint32_t *d = (const uint32_t *)b->table_base;
            switch (b->type) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                const uint64_t base = (uint64_t)d[0] | ((uint64_t)(d[1] & 0xffffu) << 32);
                vk_ps4_log("draw[%u] set%u b%u V# type=%u base=0x%llx stride=%u records=%u dw3=0x%08x",
                           draw_index, si, b->binding_number, (unsigned)b->type,
                           (unsigned long long)base, (d[1] >> 16) & 0x3fffu, d[2], d[3]);
                break;
            }
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
                const uint64_t base = ((uint64_t)d[0] | ((uint64_t)(d[1] & 0xffu) << 32)) << 8;
                vk_ps4_log("draw[%u] set%u b%u T# base=0x%llx dfmt=%u nfmt=%u %ux%u pitch=%u levels=%u..%u tiling=%u type=%u dw=[%08x %08x %08x %08x %08x %08x %08x %08x]",
                           draw_index, si, b->binding_number, (unsigned long long)base,
                           (d[1] >> 20) & 0x3fu, (d[1] >> 26) & 0xfu,
                           (d[2] & 0x3fffu) + 1u, ((d[2] >> 14) & 0x3fffu) + 1u,
                           ((d[4] >> 13) & 0x3fffu) + 1u,
                           (d[3] >> 12) & 0xfu, (d[3] >> 16) & 0xfu, (d[3] >> 20) & 0x1fu, (d[3] >> 28) & 0xfu,
                           d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
                if (b->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
                    b->descriptor_stride >= sizeof(GnmTexture) + sizeof(GnmSampler)) {
                    const uint32_t *s = (const uint32_t *)(b->table_base + sizeof(GnmTexture));
                    vk_ps4_log("draw[%u] set%u b%u S# dw=[%08x %08x %08x %08x]",
                               draw_index, si, b->binding_number, s[0], s[1], s[2], s[3]);
                }
                break;
            }
            case VK_DESCRIPTOR_TYPE_SAMPLER:
                vk_ps4_log("draw[%u] set%u b%u S# dw=[%08x %08x %08x %08x]",
                           draw_index, si, b->binding_number, d[0], d[1], d[2], d[3]);
                break;
            default:
                vk_ps4_log("draw[%u] set%u b%u type=%u dw0=0x%08x", draw_index, si, b->binding_number,
                           (unsigned)b->type, d[0]);
                break;
            }
        }
    }
    if (cmd->graphics_dynamic_table && cmd->graphics_descriptor_layout) {
        uint32_t total = 0;
        for (uint32_t s = 0; s < cmd->graphics_descriptor_layout->set_layout_count; ++s) {
            const VkPs4DescriptorSetLayout *sl = cmd->graphics_descriptor_layout->set_layouts[s];
            if (sl) total += sl->dynamic_descriptor_count;
        }
        for (uint32_t e = 0; e < total && e < 4u; ++e) {
            const uint32_t *d = (const uint32_t *)&cmd->graphics_dynamic_table[e];
            const uint64_t base = (uint64_t)d[0] | ((uint64_t)(d[1] & 0xffffu) << 32);
            vk_ps4_log("draw[%u] dynamic[%u] V# base=0x%llx stride=%u records=%u", draw_index, e,
                       (unsigned long long)base, (d[1] >> 16) & 0x3fffu, d[2]);
        }
    }
    if (cmd->graphics_push_valid[0]) {
        const uint32_t *pc = cmd->graphics_push_constants[0];
        vk_ps4_log("draw[%u] push VS dw0..15=[%08x %08x %08x %08x  %08x %08x %08x %08x  %08x %08x %08x %08x  %08x %08x %08x %08x]",
                   draw_index, pc[0], pc[1], pc[2], pc[3], pc[4], pc[5], pc[6], pc[7],
                   pc[8], pc[9], pc[10], pc[11], pc[12], pc[13], pc[14], pc[15]);
    }
    if (cmd->gnm_vertex_buffers) {
        for (uint32_t s = 0; s < cmd->vertex_descriptor_count && s < 8u; ++s) {
            const uint32_t *d = (const uint32_t *)&cmd->gnm_vertex_buffers[s];
            const uint64_t base = (uint64_t)d[0] | ((uint64_t)(d[1] & 0xffffu) << 32);
            vk_ps4_log("draw[%u] vertex[%u] V# base=0x%llx stride=%u records=%u dw3=0x%08x", draw_index, s,
                       (unsigned long long)base, (d[1] >> 16) & 0x3fffu, d[2], d[3]);
        }
    }
    {
        const VkPs4Buffer *ib = (const VkPs4Buffer *)cmd->index_buffer.buffer;
        if (ib && ib->memory && ib->memory->gnm_mem.mapped)
            vk_ps4_log("draw[%u] index buffer base=%p size=%llu offset=%llu type=%u",
                       draw_index,
                       (const void *)((const char *)ib->memory->gnm_mem.mapped + ib->memory_offset + cmd->index_buffer.offset),
                       (unsigned long long)ib->create_info.size, (unsigned long long)cmd->index_buffer.offset,
                       (unsigned)cmd->index_buffer.type);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount,
                      uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Pipeline *bound_pipe = cmd->current_pipeline;
    if (!bound_pipe || bound_pipe->rasterization_state.rasterizerDiscardEnable)
        return;
    vk_ps4_flush_graphics_user_data(cmd);
    if (cmd->recording_error != VK_SUCCESS) return;
    if (bound_pipe->vertex_input_state.vertexAttributeDescriptionCount > 0 &&
        (!bound_pipe->has_fetch_shader ||
         cmd->vertex_descriptor_count != bound_pipe->vs_input_semantic_count))
        return;

    /* Emit vertex buffer table if dirty and pipeline has a VB table slot */
    if (cmd->vertex_buffers_dirty && cmd->current_pipeline &&
        cmd->current_pipeline->has_fetch_shader &&
        cmd->current_pipeline->has_vb_table_slot &&
        cmd->vertex_descriptor_count > 0 && cmd->gnm_vertex_buffers) {
        vk_ps4_cpu_store_fence();
        sceGnmDrawCmdSetPointerUserData(
            &cmd->gnm_cmd, GNM_STAGE_VS,
            cmd->current_pipeline->vertex_buffer_table_slot,
            cmd->gnm_vertex_buffers
        );
        cmd->vertex_buffers_dirty = false;
    }

    /* Always set instance count to avoid state leak from previous draw */
    sceGnmDrawCmdSetNumInstances(&cmd->gnm_cmd, instanceCount);

    /* Emit vertexOffset and firstInstance via SET_SH_REG BEFORE the draw.
     * GCN user-data registers are read at draw time, so they must be
     * written before the draw packet. They are also sticky (persist
     * across draws), so we must always write them when the pipeline
     * uses them — even when the value is 0 — to avoid stale state
     * from a previous draw.
     * Optimization: if both registers are consecutive, emit a single
     * SET_SH_REG packet with count=2 instead of two separate packets. */
    /* See CmdDraw: the base vertex must reach the VGT so the fetch shader
     * reads the right attributes. ImGui draws its second and later draw
     * lists with a vertexOffset; with the offset only in an SGPR those
     * lists fetched the first list's vertices, and the login screen showed
     * the whole glyph atlas stretched over the backdrop quad (B4 test 8). */
    vk_ps4_emit_context_reg(&cmd->gnm_cmd, R_028408_VGT_INDX_OFFSET, (uint32_t)vertexOffset);
    if (cmd->current_pipeline) {
        VkPs4Pipeline *pipe = cmd->current_pipeline;
        bool both = pipe->has_base_vertex_reg && pipe->has_start_instance_reg &&
                    pipe->vs_base_vertex_reg + 1 == pipe->vs_start_instance_reg;
        if (both) {
            uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                pipe->vs_base_vertex_reg * 4;
            if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) < 4u &&
                    !vk_ps4_command_overflow(&cmd->gnm_cmd, 4u, cmd)) return;
                {
                cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 2, 0);
                cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                cmd->gnm_cmd.cmdptr[2] = 0u; /* carried by VGT_INDX_OFFSET */
                cmd->gnm_cmd.cmdptr[3] = firstInstance;
                cmd->gnm_cmd.cmdptr += 4;
            }
        } else {
            if (pipe->has_base_vertex_reg) {
                uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                    pipe->vs_base_vertex_reg * 4;
                if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) < 3u &&
                    !vk_ps4_command_overflow(&cmd->gnm_cmd, 3u, cmd)) return;
                {
                    cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 1, 0);
                    cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                    cmd->gnm_cmd.cmdptr[2] = 0u; /* carried by VGT_INDX_OFFSET */
                    cmd->gnm_cmd.cmdptr += 3;
                }
            }
            if (pipe->has_start_instance_reg) {
                uint32_t reg_addr = R_00B130_SPI_SHADER_USER_DATA_VS_0 +
                                    pipe->vs_start_instance_reg * 4;
                if ((uint32_t)(cmd->gnm_cmd.endptr - cmd->gnm_cmd.cmdptr) < 3u &&
                    !vk_ps4_command_overflow(&cmd->gnm_cmd, 3u, cmd)) return;
                {
                    cmd->gnm_cmd.cmdptr[0] = PKT3(PKT3_SET_SH_REG, 1, 0);
                    cmd->gnm_cmd.cmdptr[1] = (reg_addr - SI_SH_REG_OFFSET) >> 2;
                    cmd->gnm_cmd.cmdptr[2] = firstInstance;
                    cmd->gnm_cmd.cmdptr += 3;
                }
            }
        }
    }

    /* The first draws of a session, with everything they bind: a GPU fault
     * on a draw kills the process before any completion can be logged, so
     * this line, written before the submit, is what names the culprit. */
    {
        static unsigned logged_draws = 0;
        if (logged_draws < 64u) {
            ++logged_draws;
            const VkPs4Pipeline *pipe = cmd->current_pipeline;
            const void *set_tables[4] = {NULL, NULL, NULL, NULL};
            for (unsigned si = 0; si < 4u && si < VK_PS4_MAX_DESCRIPTOR_SETS; ++si) {
                VkPs4DescriptorSet *bound = cmd->bound_graphics_sets[si];
                set_tables[si] = bound ? bound->descriptor_mem.mapped : NULL;
            }
            const VkPs4Buffer *vb0 = (const VkPs4Buffer *)cmd->vertex_buffers[0].buffer;
            const void *vb0_addr = (vb0 && vb0->memory && vb0->memory->gnm_mem.mapped)
                ? (const char *)vb0->memory->gnm_mem.mapped + vb0->memory_offset + cmd->vertex_buffers[0].offset
                : NULL;
            vk_ps4_log("draw[%u]: indexed count=%u first=%u vtxoff=%d inst=%u pipe=%p vs_slots=%u tables=[%p,%p,%p,%p] vb0=%p attrs=%u",
                       logged_draws - 1u, indexCount, firstIndex, vertexOffset, instanceCount,
                       (const void *)pipe, pipe ? (unsigned)pipe->vs_input_usage_slot_count : 0u,
                       set_tables[0], set_tables[1], set_tables[2], set_tables[3], vb0_addr,
                       (unsigned)cmd->vertex_descriptor_count);
            /* The overlay's draws come first; the scene's are the ones with
             * more than two VS slots. Dump the resources of the first 16
             * such draws in full. */
            static unsigned dumped_scene_draws = 0;
            if (pipe && pipe->vs_input_usage_slot_count > 2u && dumped_scene_draws < 16u) {
                ++dumped_scene_draws;
                vk_ps4_log_draw_resources(cmd, logged_draws - 1u);
            }
        }
    }

    /* Use an explicit bounded index address for every batch. DrawIndexOffset
     * depends on sticky CP index-base/size state, whereas DRAW_INDEX_2 carries
     * its address and maximum index count in the draw packet. In B4 the first
     * M2 batch already starts at index 38268; removing INDEX_BUFFER_SIZE after
     * the hardware regression did not remove that offset draw's dependency.
     * This preserves firstIndex without relying on either sticky register.
     * vertexOffset is independently applied by VGT_INDX_OFFSET above. */
    VkPs4Buffer *buf = (VkPs4Buffer *)cmd->index_buffer.buffer;
    if (buf && buf->memory && buf->memory->gnm_mem.mapped) {
        const uint32_t index_size =
            cmd->index_buffer.type == VK_INDEX_TYPE_UINT32 ? 4u : 2u;
        const VkDeviceSize available =
            buf->create_info.size - cmd->index_buffer.offset;
        const uint64_t first_byte = (uint64_t)firstIndex * index_size;
        const uint64_t draw_bytes = (uint64_t)indexCount * index_size;
        if (first_byte > available || draw_bytes > available - first_byte)
            return;
        void *gpu_addr = (char *)buf->memory->gnm_mem.mapped + buf->memory_offset +
                         cmd->index_buffer.offset;
        GnmDrawModifier mod = {0};
        sceGnmDrawCmdDrawIndex2(&cmd->gnm_cmd, indexCount,
                               (char *)gpu_addr + first_byte, mod);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                       uint32_t drawCount, uint32_t stride) {
    if (!commandBuffer || !buffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    if (!cmd->current_pipeline) return;
    vk_ps4_flush_graphics_user_data(cmd);
    if (cmd->recording_error != VK_SUCCESS) return;

    /* Emit vertex buffer table if dirty */
    if (cmd->vertex_buffers_dirty && cmd->current_pipeline &&
        cmd->current_pipeline->has_fetch_shader &&
        cmd->current_pipeline->has_vb_table_slot &&
        cmd->vertex_binding_count > 0) {
        sceGnmDrawCmdSetPointerUserData(
            &cmd->gnm_cmd, GNM_STAGE_VS,
            cmd->current_pipeline->vertex_buffer_table_slot,
            cmd->gnm_vertex_buffers
        );
        cmd->vertex_buffers_dirty = false;
    }

    if (!buf->memory || !buf->memory->gnm_mem.mapped) return;

    /* GnmDrawIndirectArgs = { vertexCount, instanceCount, firstVertex, firstInstance }
     * This matches VkDrawIndirectCommand exactly.
     * The GNM API requires SetIndirectArgs first, then DrawIndirect with
     * an offset relative to the args buffer. */
    const GnmDrawIndirectArgs *args_base =
        (const GnmDrawIndirectArgs *)((char *)buf->memory->gnm_mem.mapped +
                                      buf->memory_offset + offset);
    sceGnmDrawCmdSetIndirectArgs(&cmd->gnm_cmd, args_base);

    for (uint32_t i = 0; i < drawCount; i++) {
        /* dataoffset is relative to the args buffer set above */
        uint32_t data_offset = i * stride;
        /* DrawIndirect reads from GPU memory at the given offset.
         * The vertexoffusgpr and instanceoffusgpr specify which VGPRs
         * contain the vertex/instance offsets. For MVP, use 0. */
        sceGnmDrawCmdDrawIndirect(
            &cmd->gnm_cmd, data_offset, GNM_STAGE_VS, 0, 0
        );
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
                              uint32_t drawCount, uint32_t stride) {
    if (!commandBuffer || !buffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    if (!cmd->current_pipeline) return;
    vk_ps4_flush_graphics_user_data(cmd);
    if (cmd->recording_error != VK_SUCCESS) return;

    /* Emit vertex buffer table if dirty */
    if (cmd->vertex_buffers_dirty && cmd->current_pipeline &&
        cmd->current_pipeline->has_fetch_shader &&
        cmd->current_pipeline->has_vb_table_slot &&
        cmd->vertex_binding_count > 0) {
        sceGnmDrawCmdSetPointerUserData(
            &cmd->gnm_cmd, GNM_STAGE_VS,
            cmd->current_pipeline->vertex_buffer_table_slot,
            cmd->gnm_vertex_buffers
        );
        cmd->vertex_buffers_dirty = false;
    }

    if (!buf->memory || !buf->memory->gnm_mem.mapped) return;

    /* GnmDrawIndexedIndirectArgs = { indexCount, instanceCount, firstIndex, vertexOffset, firstInstance }
     * This matches VkDrawIndexedIndirectCommand.
     * The GNM API requires SetIndexedIndirectArgs first, then DrawIndexIndirect
     * with an offset relative to the args buffer. */
    const GnmDrawIndexedIndirectArgs *args_base =
        (const GnmDrawIndexedIndirectArgs *)((char *)buf->memory->gnm_mem.mapped +
                                             buf->memory_offset + offset);
    sceGnmDrawCmdSetIndexedIndirectArgs(&cmd->gnm_cmd, args_base);

    for (uint32_t i = 0; i < drawCount; i++) {
        uint32_t data_offset = i * stride;
        sceGnmDrawCmdDrawIndexIndirect(
            &cmd->gnm_cmd, data_offset, GNM_STAGE_VS, 0, 0
        );
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    vk_ps4_flush_compute_user_data(cmd);
    if (cmd->recording_error != VK_SUCCESS) return;
    if (!vk_ps4_emit_dispatch_direct(&cmd->gnm_cmd, x, y, z)) {
        if (!vk_ps4_command_overflow(&cmd->gnm_cmd, 5, cmd) ||
            !vk_ps4_emit_dispatch_direct(&cmd->gnm_cmd, x, y, z)) return;
    }
    cmd->compute_dispatch_count++;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset) {
    if (!commandBuffer || !buffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Buffer *buf = (VkPs4Buffer *)buffer;
    if (!buf || !buf->memory || !buf->memory->gnm_mem.mapped) return;
    vk_ps4_flush_compute_user_data(cmd);
    if (cmd->recording_error != VK_SUCCESS) return;

    /* VkDispatchIndirectCommand = { uint32_t x; uint32_t y; uint32_t z; }
     *
     * Unlike DrawIndirect (which uses SetIndirectArgs + relative offset),
     * DISPATCH_INDIRECT takes a GPU memory address directly as the
     * dataoffset. The CP reads 3 uint32s (x, y, z) from that address.
     * The address is 32-bit. PS4 GPU memory is typically mapped in the
     * lower 4GB, but if the buffer's address exceeds 32 bits we stage
     * the 12-byte dispatch args inside the command buffer via
     * sceGnmCmdAllocInside and dispatch from there. */
    uint64_t gpu_addr = (uint64_t)((char *)buf->memory->gnm_mem.mapped +
                                    buf->memory_offset + offset);
    if (gpu_addr <= 0xFFFFFFFFULL) {
        sceGnmDrawCmdDispatchIndirect(&cmd->gnm_cmd, (uint32_t)gpu_addr, 0);
        return;
    }

    /* Address > 32 bits: stage the 12-byte VkDispatchIndirectCommand into
     * command buffer memory (which is always 32-bit addressable) and
     * dispatch from the staging copy. */
    void *src = (char *)buf->memory->gnm_mem.mapped + buf->memory_offset + offset;
    void *staging = sceGnmCmdAllocInside(&cmd->gnm_cmd, 12, 4);
    if (!staging) {
        /* AllocInside failed — cannot dispatch. This is a driver-internal
         * failure, not a spec violation. Log and skip rather than crash. */
        return;
    }
    memcpy(staging, src, 12);
    uint64_t staging_addr = (uint64_t)staging;
    /* staging_addr comes from AllocInside which is always in the 32-bit
     * command buffer address space, so the cast is safe. */
    sceGnmDrawCmdDispatchIndirect(&cmd->gnm_cmd, (uint32_t)staging_addr, 0);
}

/* === Copy/blit commands (Phase 2) === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer,
                     uint32_t regionCount, const VkBufferCopy *pRegions) {
    if (!commandBuffer || !pRegions) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Buffer *src = (VkPs4Buffer *)srcBuffer;
    VkPs4Buffer *dst = (VkPs4Buffer *)dstBuffer;

    if (!src || !dst || !src->memory || !dst->memory) return;
    if (!src->memory->gnm_mem.mapped || !dst->memory->gnm_mem.mapped) return;

    for (uint32_t i = 0; i < regionCount; i++) {
        if (pRegions[i].srcOffset > src->create_info.size ||
            pRegions[i].size > src->create_info.size - pRegions[i].srcOffset ||
            pRegions[i].dstOffset > dst->create_info.size ||
            pRegions[i].size > dst->create_info.size - pRegions[i].dstOffset) {
            cmd->recording_error = VK_ERROR_INITIALIZATION_FAILED;
            vk_ps4_log("CopyBuffer rejected: region exceeds source/destination");
            return;
        }
        uint64_t src_addr = (uint64_t)((char *)src->memory->gnm_mem.mapped +
                                        src->memory_offset + pRegions[i].srcOffset);
        uint64_t dst_addr = (uint64_t)((char *)dst->memory->gnm_mem.mapped +
                                        dst->memory_offset + pRegions[i].dstOffset);
        /* sceGnmDrawCmdCopyMemory takes uint32_t size — split large copies */
        uint64_t remaining = pRegions[i].size;
        uint64_t cur_src = src_addr;
        uint64_t cur_dst = dst_addr;
        while (remaining > 0) {
            uint32_t chunk = (remaining > 0xFFFFFFFCu) ? 0xFFFFFFFCu : (uint32_t)remaining;
            if (!sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, cur_dst, cur_src, chunk)) {
                cmd->recording_error = VK_ERROR_INITIALIZATION_FAILED;
                vk_ps4_log("CopyBuffer rejected: src=%llx dst=%llx bytes=%u (alignment or command capacity)",
                           (unsigned long long)cur_src, (unsigned long long)cur_dst, chunk);
                return;
            }
            cur_src += chunk;
            cur_dst += chunk;
            remaining -= chunk;
        }
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                     VkDeviceSize dstOffset, VkDeviceSize fillSize, uint32_t data) {
    if (!commandBuffer || !dstBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Buffer *dst = (VkPs4Buffer *)dstBuffer;
    if (!dst || !dst->memory || !dst->memory->gnm_mem.mapped) return;

    uint64_t dst_addr = (uint64_t)((char *)dst->memory->gnm_mem.mapped +
                                    dst->memory_offset + dstOffset);

    /* VK_WHOLE_SIZE means fill from dstOffset to the end of the buffer.
     * Guard against dstOffset > buffer size to prevent unsigned underflow. */
    if (dstOffset >= dst->create_info.size) return;
    uint64_t size = fillSize;
    if (fillSize == VK_WHOLE_SIZE) {
        size = dst->create_info.size - dstOffset;
    }
    /* Clamp to remaining buffer space */
    if (dstOffset + size > dst->create_info.size) {
        size = dst->create_info.size - dstOffset;
    }
    /* Align size to 4 bytes (FillMemory writes 32-bit values) */
    size = (size + 3) & ~3ULL;

    /* sceGnmDrawCmdFillMemory takes uint32_t size — split large fills */
    uint64_t remaining = size;
    uint64_t cur = dst_addr;
    while (remaining > 0) {
        uint32_t chunk = (remaining > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)remaining;
        /* Align chunk to 4 bytes */
        chunk &= ~3u;
        if (chunk == 0) break;
        sceGnmDrawCmdFillMemory(&cmd->gnm_cmd, cur, chunk, data);
        cur += chunk;
        remaining -= chunk;
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                       VkDeviceSize dstOffset, VkDeviceSize dataSize, const void *pData) {
    if (!commandBuffer || !dstBuffer || !pData) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Buffer *dst = (VkPs4Buffer *)dstBuffer;
    if (!dst || !dst->memory || !dst->memory->gnm_mem.mapped) return;

    uint64_t dst_addr = (uint64_t)((char *)dst->memory->gnm_mem.mapped +
                                    dst->memory_offset + dstOffset);

    /* CmdUpdateBuffer is limited to 65536 bytes per the Vulkan spec.
     *
     * Phase 3: Stage the data inside the command buffer via
     * sceGnmCmdAllocInside, which embeds the data in the GPU-visible
     * command buffer memory (wrapped in a NOP packet so the CP skips it).
     * Then emit a CopyMemory to copy from the staging area to the
     * destination at submit time.  This is semantically correct — the
     * update happens when the command buffer is executed, not at record
     * time.
     *
     * Fallback: if AllocInside fails (command buffer full), fall back to
     * the old CPU memcpy approach.  This is safe when the destination is
     * not being read by the GPU. */
    uint64_t size = dataSize;
    if (size > 65536) size = 65536;  /* clamp to spec limit */
    if (size == 0) return;

    /* Round up to 4 bytes for AllocInside alignment. */
    uint32_t alloc_size = (uint32_t)((size + 3) & ~3ull);
    void *staging = sceGnmCmdAllocInside(&cmd->gnm_cmd, alloc_size, 4);
    if (staging) {
        /* Copy the caller's data into the staging area (CPU-visible
         * command buffer memory), then emit a GPU copy from staging to
         * the destination.  The copy executes at submit time. */
        memcpy(staging, pData, (size_t)size);
        uint64_t src_addr = (uint64_t)staging;
        /* CopyMemory takes uint32_t size — split large copies. */
        uint64_t remaining = size;
        uint64_t cur_src = src_addr;
        uint64_t cur_dst = dst_addr;
        while (remaining > 0) {
            uint32_t chunk = (remaining > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)remaining;
            sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, cur_dst, cur_src, chunk);
            cur_src += chunk;
            cur_dst += chunk;
            remaining -= chunk;
        }
    } else {
        vk_ps4_command_fail(cmd, "buffer update staging exhausted");
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                    VkImage dstImage, VkImageLayout dstImageLayout,
                    uint32_t regionCount, const VkImageCopy *pRegions) {
    (void)srcImageLayout; (void)dstImageLayout;
    if (!commandBuffer || !pRegions) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Image *src = (VkPs4Image *)srcImage, *dst = (VkPs4Image *)dstImage;
    if (!src || !dst || !src->memory || !dst->memory ||
        !src->memory->gnm_mem.mapped || !dst->memory->gnm_mem.mapped ||
        src->create_info.format != dst->create_info.format ||
        src->create_info.samples != VK_SAMPLE_COUNT_1_BIT || dst->create_info.samples != VK_SAMPLE_COUNT_1_BIT) {
        vk_ps4_command_fail(cmd, "image copy unbound/format/sample mismatch"); return;
    }
    const uint64_t src_base = (uint64_t)src->memory->gnm_mem.mapped + src->memory_offset;
    const uint64_t dst_base = (uint64_t)dst->memory->gnm_mem.mapped + dst->memory_offset;
    for (uint32_t i = 0; i < regionCount; ++i) {
        const VkImageCopy *r = &pRegions[i];
        uint32_t src_layers, dst_layers;
        if (!vk_ps4_resolve_range(r->srcSubresource.baseArrayLayer, r->srcSubresource.layerCount,
                                 src->create_info.arrayLayers, &src_layers) ||
            !vk_ps4_resolve_range(r->dstSubresource.baseArrayLayer, r->dstSubresource.layerCount,
                                 dst->create_info.arrayLayers, &dst_layers) || src_layers != dst_layers ||
            r->srcSubresource.mipLevel >= src->create_info.mipLevels ||
            r->dstSubresource.mipLevel >= dst->create_info.mipLevels ||
            r->srcSubresource.aspectMask != r->dstSubresource.aspectMask ||
            r->srcOffset.x < 0 || r->srcOffset.y < 0 || r->srcOffset.z != 0 ||
            r->dstOffset.x < 0 || r->dstOffset.y < 0 || r->dstOffset.z != 0 || r->extent.depth != 1) {
            vk_ps4_command_fail(cmd, "image copy subresource invalid/unsupported"); return;
        }
        const uint32_t sw = src->create_info.extent.width >> r->srcSubresource.mipLevel;
        const uint32_t sh = src->create_info.extent.height >> r->srcSubresource.mipLevel;
        const uint32_t dw = dst->create_info.extent.width >> r->dstSubresource.mipLevel;
        const uint32_t dh = dst->create_info.extent.height >> r->dstSubresource.mipLevel;
        const VkRect2D sr = {{r->srcOffset.x,r->srcOffset.y}, {r->extent.width,r->extent.height}};
        const VkRect2D dr = {{r->dstOffset.x,r->dstOffset.y}, {r->extent.width,r->extent.height}};
        if (!vk_ps4_rect_in_surface(&sr, sw ? sw : 1, sh ? sh : 1) ||
            !vk_ps4_rect_in_surface(&dr, dw ? dw : 1, dh ? dh : 1)) {
            vk_ps4_command_fail(cmd, "image copy rectangle out of range"); return;
        }
        const bool full = !r->srcOffset.x && !r->srcOffset.y && !r->dstOffset.x && !r->dstOffset.y &&
            r->extent.width == sw && r->extent.width == dw &&
            r->extent.height == sh && r->extent.height == dh;
        const bool attachments = src->is_render_target || src->is_depth_target;
        const bool same_class = src->is_render_target == dst->is_render_target &&
            src->is_depth_target == dst->is_depth_target;
        const bool same_target_layout = same_class &&
            ((src->is_render_target && src->gnm_rt.attrib.tilemode_index == dst->gnm_rt.attrib.tilemode_index &&
              src->gnm_rt.pitch.asuint == dst->gnm_rt.pitch.asuint && src->gnm_rt.slice.asuint == dst->gnm_rt.slice.asuint) ||
             (src->is_depth_target && src->gnm_drt.zinfo.asuint == dst->gnm_drt.zinfo.asuint &&
              src->gnm_drt.depthsize.asuint == dst->gnm_drt.depthsize.asuint &&
              src->gnm_drt.depthslice.asuint == dst->gnm_drt.depthslice.asuint));
        if (attachments && full && same_target_layout && src->create_info.arrayLayers == 1 &&
            dst->create_info.arrayLayers == 1 && src->create_info.mipLevels == 1 && dst->create_info.mipLevels == 1 &&
            (!src->is_depth_target || !vk_format_has_stencil(src->create_info.format) ||
             r->srcSubresource.aspectMask == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) {
            VkMemoryRequirements sq = {0}, dq = {0};
            vk_ps4_GetImageMemoryRequirements((VkDevice)cmd->device, srcImage, &sq);
            vk_ps4_GetImageMemoryRequirements((VkDevice)cmd->device, dstImage, &dq);
            if (sq.size && sq.size == dq.size && sq.size <= UINT32_MAX &&
                src->memory_offset <= src->memory->size && sq.size <= src->memory->size - src->memory_offset &&
                dst->memory_offset <= dst->memory->size && dq.size <= dst->memory->size - dst->memory_offset) {
                sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, dst_base, src_base, (uint32_t)sq.size);
                continue;
            }
        }
        if (!attachments && same_class && full &&
            src->gnm_texture.tilingindex == dst->gnm_texture.tilingindex &&
            src->create_info.extent.width == dst->create_info.extent.width &&
            src->create_info.extent.height == dst->create_info.extent.height &&
            src->create_info.mipLevels == dst->create_info.mipLevels &&
            src->gnm_texture.pitch == dst->gnm_texture.pitch &&
            r->srcSubresource.mipLevel == r->dstSubresource.mipLevel) {
            const GpaTextureInfo si = sceGnmTexBuildInfo(&src->gnm_texture);
            const GpaTextureInfo di = sceGnmTexBuildInfo(&dst->gnm_texture);
            for (uint32_t layer = 0; layer < src_layers; ++layer) {
                uint64_t size = 0, dsize = 0, so = 0, doff = 0;
                if (sceGpaComputeSurfaceSizeOffset(&size, &so, &si, r->srcSubresource.mipLevel,
                        r->srcSubresource.baseArrayLayer + layer) != GPA_ERR_OK ||
                    sceGpaComputeSurfaceSizeOffset(&dsize, &doff, &di, r->dstSubresource.mipLevel,
                        r->dstSubresource.baseArrayLayer + layer) != GPA_ERR_OK ||
                    size != dsize || size > UINT32_MAX || src->memory_offset > src->memory->size ||
                    so > src->memory->size - src->memory_offset || size > src->memory->size - src->memory_offset - so ||
                    dst->memory_offset > dst->memory->size || doff > dst->memory->size - dst->memory_offset ||
                    dsize > dst->memory->size - dst->memory_offset - doff) {
                    vk_ps4_command_fail(cmd, "image copy texture address-library mismatch"); return;
                }
                sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, dst_base + doff, src_base + so, (uint32_t)size);
            }
            continue;
        }
        if (vk_ps4_image_has_linear_storage(src) && vk_ps4_image_has_linear_storage(dst) &&
            src->create_info.mipLevels == 1 && dst->create_info.mipLevels == 1 &&
            src->create_info.arrayLayers == 1 && dst->create_info.arrayLayers == 1 &&
            !vk_format_is_compressed(src->create_info.format)) {
            const uint32_t bpp = vk_format_to_bpp(src->create_info.format);
            const uint64_t sp = (src->is_render_target ? sceGnmRtGetPitch(&src->gnm_rt) : sceGnmTexGetPitch(&src->gnm_texture)) * (uint64_t)bpp;
            const uint64_t dp = (dst->is_render_target ? sceGnmRtGetPitch(&dst->gnm_rt) : sceGnmTexGetPitch(&dst->gnm_texture)) * (uint64_t)bpp;
            const uint64_t row_size = (uint64_t)r->extent.width * bpp;
            for (uint32_t y = 0; y < r->extent.height; ++y) {
                const uint64_t so = ((uint64_t)r->srcOffset.y + y)*sp + (uint64_t)r->srcOffset.x*bpp;
                const uint64_t doff = ((uint64_t)r->dstOffset.y + y)*dp + (uint64_t)r->dstOffset.x*bpp;
                if (src->memory_offset > src->memory->size || so > src->memory->size-src->memory_offset ||
                    row_size > src->memory->size-src->memory_offset-so || dst->memory_offset > dst->memory->size ||
                    doff > dst->memory->size-dst->memory_offset || row_size > dst->memory->size-dst->memory_offset-doff ||
                    row_size > UINT32_MAX) {
                    vk_ps4_command_fail(cmd, "linear image copy outside allocation"); return;
                }
                sceGnmDrawCmdCopyMemory(&cmd->gnm_cmd, dst_base+doff, src_base+so, (uint32_t)row_size);
            }
            continue;
        }
        vk_ps4_command_fail(cmd, "image copy requires unsupported tiled conversion or region"); return;
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                    VkImage dstImage, VkImageLayout dstImageLayout,
                    uint32_t regionCount, const VkImageBlit *pRegions, VkFilter filter) {
    /* Copy-equivalent translated 1:1 regions are supported. Scaling/mirroring
     * requires a GPU meta-shader and fails command recording explicitly. */
    if (!commandBuffer || !pRegions) return;

    bool is_1to1 = true;
    for (uint32_t i = 0; i < regionCount; i++) {
        const VkImageBlit *r = &pRegions[i];
        if (r->srcOffsets[1].x <= r->srcOffsets[0].x ||
            r->srcOffsets[1].y <= r->srcOffsets[0].y ||
            r->srcOffsets[1].z <= r->srcOffsets[0].z ||
            (r->srcOffsets[1].x - r->srcOffsets[0].x) != (r->dstOffsets[1].x - r->dstOffsets[0].x) ||
            (r->srcOffsets[1].y - r->srcOffsets[0].y) != (r->dstOffsets[1].y - r->dstOffsets[0].y) ||
            (r->srcOffsets[1].z - r->srcOffsets[0].z) != (r->dstOffsets[1].z - r->dstOffsets[0].z)) {
            is_1to1 = false;
            break;
        }
    }

    if (is_1to1) {
        /* Process in chunks of 16 to avoid stack overflow on large region counts */
        for (uint32_t chunk_start = 0; chunk_start < regionCount; chunk_start += 16) {
            VkImageCopy copies[16];
            uint32_t count = (regionCount - chunk_start > 16) ? 16 : (regionCount - chunk_start);
            for (uint32_t i = 0; i < count; i++) {
                const VkImageBlit *r = &pRegions[chunk_start + i];
                copies[i].srcSubresource = r->srcSubresource;
                copies[i].dstSubresource = r->dstSubresource;
                copies[i].srcOffset = r->srcOffsets[0];
                copies[i].dstOffset = r->dstOffsets[0];
                copies[i].extent.width = r->srcOffsets[1].x - r->srcOffsets[0].x;
                copies[i].extent.height = r->srcOffsets[1].y - r->srcOffsets[0].y;
                copies[i].extent.depth = r->srcOffsets[1].z - r->srcOffsets[0].z;
            }
            vk_ps4_CmdCopyImage(commandBuffer, srcImage, srcImageLayout,
                                dstImage, dstImageLayout, count, copies);
        }
    }
    if (!is_1to1) vk_ps4_command_fail((VkPs4CommandBuffer *)commandBuffer,
        "scaled/mirrored image blit requires GPU meta-shader (not supported)");
    (void)filter;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                       VkImage dstImage, VkImageLayout dstImageLayout,
                       uint32_t regionCount, const VkImageResolve *pRegions) {
    (void)srcImage; (void)srcImageLayout; (void)dstImage; (void)dstImageLayout;
    (void)regionCount; (void)pRegions;
    vk_ps4_command_fail((VkPs4CommandBuffer *)commandBuffer,
        "MSAA resolve unsupported; single-sample image copy must use CmdCopyImage");
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                            VkImageLayout dstImageLayout, uint32_t regionCount,
                            const VkBufferImageCopy *pRegions) {
    if (!commandBuffer || !pRegions) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Buffer *src = (VkPs4Buffer *)srcBuffer;
    VkPs4Image *dst = (VkPs4Image *)dstImage;
    if (!src || !dst || !src->memory || !dst->memory) return;
    if (!src->memory->gnm_mem.mapped || !dst->memory->gnm_mem.mapped) return;

    const bool linear_image = vk_ps4_image_has_linear_storage(dst);

    /* OPTIMAL images use AMD's tiled surface layout. Convert each complete
     * mip/layer with OpenGNM's address library. This runs while recording,
     * but Vulkan staging lifetime rules still apply and WoWee only uploads
     * newly created images here, before they can be submitted for sampling. */
    if (!linear_image) {
        if (dst->is_render_target || dst->is_depth_target ||
            dst->is_swapchain_image) {
            vk_ps4_command_fail(cmd, "tiled attachment buffer upload unsupported");
            return;
        }

        const GpaTextureInfo dst_info = sceGnmTexBuildInfo(&dst->gnm_texture);
        for (uint32_t i = 0; i < regionCount; ++i) {
            const VkBufferImageCopy *r = &pRegions[i];
            const uint32_t mip = r->imageSubresource.mipLevel;
            if (mip >= dst->create_info.mipLevels || mip >= 32) {
                vk_ps4_command_fail(cmd, "texture upload mip out of range"); return;
            }
            const uint32_t mip_width = dst->create_info.extent.width >> mip;
            const uint32_t mip_height = dst->create_info.extent.height >> mip;
            const uint32_t expected_width = mip_width ? mip_width : 1u;
            const uint32_t expected_height = mip_height ? mip_height : 1u;
            uint32_t layers = r->imageSubresource.layerCount;
            if (layers == VK_REMAINING_ARRAY_LAYERS)
                layers = dst->create_info.arrayLayers -
                         r->imageSubresource.baseArrayLayer;

            if ((r->imageSubresource.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) == 0 ||
                mip >= dst->create_info.mipLevels || layers == 0 ||
                r->imageOffset.x != 0 || r->imageOffset.y != 0 ||
                r->imageOffset.z != 0 || r->imageExtent.depth != 1 ||
                r->imageExtent.width != expected_width ||
                r->imageExtent.height != expected_height ||
                r->bufferRowLength != 0 || r->bufferImageHeight != 0 ||
                r->imageSubresource.baseArrayLayer > dst->create_info.arrayLayers ||
                layers > dst->create_info.arrayLayers -
                         r->imageSubresource.baseArrayLayer) {
                vk_ps4_log("CmdCopyBufferToImage: tiled region=%u is not a complete subresource", i);
                vk_ps4_command_fail(cmd, "texture upload requires complete mip/layer");
                return;
            }

            GpaTextureInfo src_info = dst_info;
            src_info.tm = GNM_TM_DISPLAY_LINEAR_GENERAL;
            src_info.pitch = dst_info.width;
            const uint32_t texel_bytes =
                sceGnmDfGetBytesPerElement(sceGnmTexGetFormat(&dst->gnm_texture));

            for (uint32_t layer = 0; layer < layers; ++layer) {
                const uint32_t array_slice =
                    r->imageSubresource.baseArrayLayer + layer;
                GpaTilingParams src_tp;
                GpaTilingParams dst_tp;
                uint64_t src_size = 0, src_offset = 0;
                uint64_t dst_size = 0, dst_offset = 0;
                memset(&src_tp, 0, sizeof(src_tp));
                if (sceGpaTpInit(&dst_tp, &dst_info, mip, array_slice) != GPA_ERR_OK ||
                    sceGpaComputeSurfaceSizeOffset(&dst_size, &dst_offset,
                                                   &dst_info, mip, array_slice) != GPA_ERR_OK) {
                    vk_ps4_log("CmdCopyBufferToImage: surface setup failed mip=%u layer=%u",
                               mip, array_slice);
                    vk_ps4_command_fail(cmd, "texture upload address-library setup failed");
                    return;
                }
                /* The mode the hardware reads this level with (see the tiler
                 * above); the library's tiler needs it too. */
                GpaSurfaceInfo dst_surface;
                memset(&dst_surface, 0, sizeof(dst_surface));
                if (sceGpaComputeSurfaceInfo(&dst_surface, &dst_tp) == GPA_ERR_OK &&
                    dst_surface.tilemode != 0)
                    dst_tp.tilemode = dst_surface.tilemode;
                const bool micro_tiled_level =
                    dst_tp.tilemode == GNM_TM_DISPLAY_1D_THIN && texel_bytes == 4u;
                if (micro_tiled_level) {
                    /* One tightly packed level; no linear surface description
                     * needed (the library refuses to describe levels under 8x8). */
                    src_size = (uint64_t)expected_width * expected_height * 4u;
                } else if (sceGpaTpInit(&src_tp, &src_info, mip, array_slice) != GPA_ERR_OK ||
                           sceGpaComputeSurfaceSizeOffset(&src_size, &src_offset,
                                                          &src_info, mip, array_slice) != GPA_ERR_OK) {
                    vk_ps4_log("CmdCopyBufferToImage: source setup failed mip=%u layer=%u",
                               mip, array_slice);
                    vk_ps4_command_fail(cmd, "texture upload address-library setup failed");
                    return;
                }

                /* The caller supplies one tightly packed subresource. Offset
                 * within the staging buffer advances by its linear size. */
                const uint64_t src_begin = r->bufferOffset +
                    (uint64_t)layer * src_size;
                if (src_begin > src->create_info.size ||
                    src_size > src->create_info.size - src_begin ||
                    dst_offset > dst->memory->size - dst->memory_offset ||
                    dst_size > dst->memory->size - dst->memory_offset - dst_offset) {
                    vk_ps4_log("CmdCopyBufferToImage: tiled bounds failed mip=%u layer=%u",
                               mip, array_slice);
                    vk_ps4_command_fail(cmd, "texture upload allocation bounds failed");
                    return;
                }

                const void *src_pixels =
                    (const char *)src->memory->gnm_mem.mapped +
                    src->memory_offset + src_begin;
                void *dst_pixels =
                    (char *)dst->memory->gnm_mem.mapped +
                    dst->memory_offset + dst_offset;
                /* TileSurface addresses are relative to the supplied mip
                 * pointers; the offsets above select the actual subresource. */
                if (micro_tiled_level) {
                    vk_ps4_tile_1d_thin_32bpp((const uint8_t *)src_pixels, expected_width,
                                              expected_height, (uint8_t *)dst_pixels,
                                              (size_t)dst_size);
                } else if (sceGpaTileSurface(dst_pixels, (size_t)dst_size,
                                             src_pixels, (size_t)src_size,
                                             &src_tp, &dst_tp) != GPA_ERR_OK) {
                    vk_ps4_log("CmdCopyBufferToImage: tiling failed mip=%u layer=%u",
                               mip, array_slice);
                    vk_ps4_command_fail(cmd, "texture upload tiling failed");
                    return;
                } else {
                    /* DIAGNOSTIC (texture-mapping investigation): log the
                     * first few bytes actually written into the tiled
                     * destination, straight from src_pixels (known-good
                     * linear source) so a hardware log shows whether real,
                     * non-zero pixel data reached this upload at all - same
                     * question as the swapchain pixel dump, one step
                     * earlier in the pipeline. */
                    static uint32_t tex_upload_log_count = 0;
                    if (tex_upload_log_count < 24) {
                        tex_upload_log_count++;
                        const uint8_t *sp = (const uint8_t *)src_pixels;
                        vk_ps4_log("CmdCopyBufferToImage: tiled OK mip=%u layer=%u "
                                   "src_size=%llu dst_size=%llu src[0..3]=%02x%02x%02x%02x",
                                   mip, array_slice,
                                   (unsigned long long)src_size, (unsigned long long)dst_size,
                                   sp[0], sp[1], sp[2], sp[3]);
                    }
                }
            }
        }
        vk_ps4_cpu_store_fence();
        (void)cmd;
        (void)dstImageLayout;
        return;
    }

    if (!vk_ps4_format_is_linear_rgba8(dst->create_info.format)) {
        vk_ps4_command_fail(cmd, "unsupported linear image upload format");
        return;
    }
    if (src->memory_offset > src->memory->size ||
        src->create_info.size > src->memory->size - src->memory_offset ||
        dst->memory_offset > dst->memory->size) {
        vk_ps4_command_fail(cmd, "linear upload allocation binding out of range");
        return;
    }

    /* A transfer can target a swapchain image without entering a render
     * pass. Synchronize explicitly with the display engine before the GPU
     * overwrites the currently scanned-out VideoOut allocation. */
    if (dst->is_swapchain_image) {
        sceGnmDrawCmdWaitUntilSafeForRendering(
            &cmd->gnm_cmd,
            dst->video_out_handle,
            dst->swapchain_buffer_index
        );
    }

    const uint32_t dst_pitch = (dst->is_swapchain_image || dst->is_render_target)
        ? sceGnmRtGetPitch(&dst->gnm_rt) * 4u
        : sceGnmTexGetPitch(&dst->gnm_texture) * 4u;
    if (dst_pitch == 0) {
        vk_ps4_command_fail(cmd, "linear upload has no destination pitch"); return;
    }

    /* Publish CPU writes to WC staging allocations before CP DMA reads them.
     * QueueSubmit also fences the final PM4 stream, but this marker documents
     * and enforces the resource-upload ordering contract independently. */
    vk_ps4_cpu_store_fence();

    for (uint32_t i = 0; i < regionCount; i++) {
        const VkBufferImageCopy *r = &pRegions[i];
        if ((r->imageSubresource.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) == 0 ||
            r->imageSubresource.mipLevel != 0 ||
            r->imageSubresource.baseArrayLayer != 0 ||
            r->imageSubresource.layerCount != 1 ||
            r->imageOffset.x < 0 || r->imageOffset.y < 0 || r->imageOffset.z != 0 ||
            r->imageExtent.width == 0 || r->imageExtent.height == 0 ||
            r->imageExtent.depth != 1 ||
            (uint32_t)r->imageOffset.x > dst->create_info.extent.width ||
            r->imageExtent.width > dst->create_info.extent.width - (uint32_t)r->imageOffset.x ||
            (uint32_t)r->imageOffset.y > dst->create_info.extent.height ||
            r->imageExtent.height > dst->create_info.extent.height - (uint32_t)r->imageOffset.y ||
            (r->bufferRowLength != 0 && r->bufferRowLength < r->imageExtent.width) ||
            (r->bufferImageHeight != 0 && r->bufferImageHeight < r->imageExtent.height)) {
            vk_ps4_log("CmdCopyBufferToImage: rejected region=%u", i);
            vk_ps4_command_fail(cmd, "linear upload rectangle out of range"); return;
        }

        const uint32_t row_pixels = r->bufferRowLength
            ? r->bufferRowLength : r->imageExtent.width;
        if (row_pixels > UINT32_MAX / 4u ||
            r->imageExtent.width > UINT32_MAX / 4u) {
            vk_ps4_command_fail(cmd, "linear upload row size overflow"); return;
        }
        const uint32_t src_pitch = row_pixels * 4u;
        const uint32_t copy_width = r->imageExtent.width * 4u;
        const uint64_t src_span =
            (uint64_t)(r->imageExtent.height - 1u) * src_pitch + copy_width;
        const uint64_t dst_last =
            (uint64_t)(r->imageOffset.y + (int32_t)r->imageExtent.height - 1) * dst_pitch +
            (uint64_t)r->imageOffset.x * 4u + copy_width;
        if (r->bufferOffset > src->create_info.size ||
            src_span > src->create_info.size - r->bufferOffset ||
            dst_last > dst->memory->size - dst->memory_offset) {
            vk_ps4_log("CmdCopyBufferToImage: region=%u out of bounds", i);
            vk_ps4_command_fail(cmd, "linear upload buffer bounds exceeded"); return;
        }

        uint64_t src_base = (uint64_t)src->memory->gnm_mem.mapped +
                            src->memory_offset + r->bufferOffset;
        uint64_t dst_base = (uint64_t)dst->memory->gnm_mem.mapped +
                            dst->memory_offset;

        for (uint32_t y = 0; y < r->imageExtent.height; y++) {
            const uint64_t src_addr = src_base + (uint64_t)y * src_pitch;
            const uint64_t dst_addr = dst_base +
                (uint64_t)(r->imageOffset.y + (int32_t)y) * dst_pitch +
                (uint64_t)r->imageOffset.x * 4u;
            if (!sceGnmDrawCmdCopyMemory(
                    &cmd->gnm_cmd, dst_addr, src_addr, copy_width)) {
                vk_ps4_log("CmdCopyBufferToImage: PM4 overflow row=%u", y);
                if (cmd->recording_error == VK_SUCCESS)
                    cmd->recording_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
                return;
            }
        }
    }
    (void)dstImageLayout;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                            VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy *pRegions) {
    if (!commandBuffer || !pRegions) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Image *src = (VkPs4Image *)srcImage;
    VkPs4Buffer *dst = (VkPs4Buffer *)dstBuffer;
    if (!src || !dst || !src->memory || !dst->memory) return;
    if (!src->memory->gnm_mem.mapped || !dst->memory->gnm_mem.mapped) return;

    if (!vk_ps4_image_has_linear_storage(src) ||
        !vk_ps4_format_is_linear_rgba8(src->create_info.format)) {
        vk_ps4_log_raw("CmdCopyImageToBuffer: rejected non-linear/non-RGBA8 image");
        return;
    }

    const uint32_t src_pitch = (src->is_swapchain_image || src->is_render_target)
        ? sceGnmRtGetPitch(&src->gnm_rt) * 4u
        : sceGnmTexGetPitch(&src->gnm_texture) * 4u;
    if (src_pitch == 0) return;

    for (uint32_t i = 0; i < regionCount; i++) {
        const VkBufferImageCopy *r = &pRegions[i];
        if ((r->imageSubresource.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) == 0 ||
            r->imageSubresource.mipLevel != 0 ||
            r->imageSubresource.baseArrayLayer != 0 ||
            r->imageSubresource.layerCount != 1 ||
            r->imageOffset.x < 0 || r->imageOffset.y < 0 || r->imageOffset.z != 0 ||
            r->imageExtent.width == 0 || r->imageExtent.height == 0 ||
            r->imageExtent.depth != 1 ||
            (uint32_t)r->imageOffset.x > src->create_info.extent.width ||
            r->imageExtent.width > src->create_info.extent.width - (uint32_t)r->imageOffset.x ||
            (uint32_t)r->imageOffset.y > src->create_info.extent.height ||
            r->imageExtent.height > src->create_info.extent.height - (uint32_t)r->imageOffset.y ||
            (r->bufferRowLength != 0 && r->bufferRowLength < r->imageExtent.width) ||
            (r->bufferImageHeight != 0 && r->bufferImageHeight < r->imageExtent.height))
            continue;

        const uint32_t row_pixels = r->bufferRowLength
            ? r->bufferRowLength : r->imageExtent.width;
        if (row_pixels > UINT32_MAX / 4u ||
            r->imageExtent.width > UINT32_MAX / 4u)
            continue;
        const uint32_t dst_pitch = row_pixels * 4u;
        const uint32_t copy_width = r->imageExtent.width * 4u;
        const uint64_t src_last =
            (uint64_t)(r->imageOffset.y + (int32_t)r->imageExtent.height - 1) * src_pitch +
            (uint64_t)r->imageOffset.x * 4u + copy_width;
        const uint64_t dst_last = r->bufferOffset +
            (uint64_t)(r->imageExtent.height - 1u) * dst_pitch + copy_width;
        if (src_last > src->memory->size - src->memory_offset ||
            dst_last > dst->create_info.size)
            continue;

        uint64_t src_base = (uint64_t)src->memory->gnm_mem.mapped +
                            src->memory_offset;
        uint64_t dst_base = (uint64_t)dst->memory->gnm_mem.mapped +
                            dst->memory_offset + r->bufferOffset;

        for (uint32_t y = 0; y < r->imageExtent.height; y++) {
            const uint64_t src_addr = src_base +
                (uint64_t)(r->imageOffset.y + (int32_t)y) * src_pitch +
                (uint64_t)r->imageOffset.x * 4u;
            const uint64_t dst_addr = dst_base + (uint64_t)y * dst_pitch;
            if (!sceGnmDrawCmdCopyMemory(
                    &cmd->gnm_cmd, dst_addr, src_addr, copy_width))
                break;
        }
    }
    (void)srcImageLayout;
}

/* === Render pass commands === */

/* Check if attachment `att_idx` is used by subpass `subpass` (via any
 * color, depth/stencil, input, or resolve attachment reference). */
static bool vk_ps4_attachment_used_in_subpass(VkPs4RenderPass *rp,
                                               uint32_t att_idx, uint32_t subpass) {
    if (subpass >= rp->subpass_count) return false;
    const VkSubpassDescription *sp = &rp->subpasses[subpass];

    if (sp->pColorAttachments) {
        for (uint32_t i = 0; i < sp->colorAttachmentCount; i++) {
            if (sp->pColorAttachments[i].attachment == att_idx) return true;
        }
    }
    if (sp->pDepthStencilAttachment &&
        sp->pDepthStencilAttachment->attachment == att_idx) {
        return true;
    }
    if (sp->pInputAttachments) {
        for (uint32_t i = 0; i < sp->inputAttachmentCount; i++) {
            if (sp->pInputAttachments[i].attachment == att_idx) return true;
        }
    }
    if (sp->pResolveAttachments) {
        for (uint32_t i = 0; i < sp->colorAttachmentCount; i++) {
            if (sp->pResolveAttachments[i].attachment == att_idx) return true;
        }
    }
    return false;
}

/* Check if attachment `att_idx` is used in any subpass before `subpass`. */
static bool vk_ps4_attachment_used_before_subpass(VkPs4RenderPass *rp,
                                                   uint32_t att_idx, uint32_t subpass) {
    for (uint32_t s = 0; s < subpass; s++) {
        if (vk_ps4_attachment_used_in_subpass(rp, att_idx, s)) return true;
    }
    return false;
}

static void vk_ps4_clear_subpass_loads(VkPs4CommandBuffer *cmd) {
    VkPs4RenderPass *rp = cmd->current_render_pass.pass;
    VkPs4Framebuffer *fb = cmd->current_render_pass.framebuffer;
    if (!rp || !fb) return;
    const uint32_t subpass = cmd->current_render_pass.current_subpass;
    for (uint32_t i = 0; i < rp->attachment_count && i < fb->attachment_count; ++i) {
        if (!vk_ps4_attachment_used_in_subpass(rp, i, subpass) ||
            vk_ps4_attachment_used_before_subpass(rp, i, subpass)) continue;
        VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, i);
        if (!view || !view->image) continue;
        VkImageAspectFlags aspects = 0;
        const VkFormat format = view->image->create_info.format;
        if (view->image->is_depth_target) {
            if (vk_format_has_depth(format) && rp->attachments[i].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
                aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
            if (vk_format_has_stencil(format) && rp->attachments[i].stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
                aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
        } else if (rp->attachments[i].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            aspects = VK_IMAGE_ASPECT_COLOR_BIT;
        }
        if (!aspects) continue;
        if (i >= cmd->current_render_pass.clear_value_count || fb->layers > view->create_info.subresourceRange.layerCount) {
            vk_ps4_command_fail(cmd, "render pass clear values/layers missing");
            return;
        }
        vk_ps4_clear_region(cmd, view->image, aspects, &cmd->current_render_pass.clear_values[i],
            &cmd->current_render_pass.render_area, view->create_info.subresourceRange.baseArrayLayer,
            fb->layers);
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pBeginInfo,
                          VkSubpassContents contents) {
    (void)contents;
    if (!commandBuffer || !pBeginInfo) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4RenderPass *rp = (VkPs4RenderPass *)pBeginInfo->renderPass;
    VkPs4Framebuffer *fb = (VkPs4Framebuffer *)pBeginInfo->framebuffer;

    if (!rp || !fb) return;

    cmd->current_render_pass.pass = rp;
    cmd->current_render_pass.framebuffer = fb;
    cmd->current_render_pass.render_area = pBeginInfo->renderArea;
    cmd->current_render_pass.current_subpass = 0;

    /* For imageless framebuffers, extract attachment views from
     * VkRenderPassAttachmentBeginInfo in the pNext chain. */
    cmd->current_render_pass.imageless_attachment_count = 0;
    if (fb->imageless) {
        VkBaseInStructure *chain = (VkBaseInStructure *)pBeginInfo->pNext;
        while (chain) {
            if (chain->sType == VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO) {
                VkRenderPassAttachmentBeginInfo *att_begin =
                    (VkRenderPassAttachmentBeginInfo *)chain;
                uint32_t count = att_begin->attachmentCount;
                if (count > 16) count = 16;
                for (uint32_t i = 0; i < count; i++) {
                    cmd->current_render_pass.imageless_attachments[i] =
                        (VkPs4ImageView *)att_begin->pAttachments[i];
                }
                cmd->current_render_pass.imageless_attachment_count = count;
                break;
            }
            chain = (VkBaseInStructure *)chain->pNext;
        }
    }

    /* Deep-copy clear values — the caller's pClearValues may be freed
     * after CmdBeginRenderPass returns, but CmdNextSubpass may need
     * them later for attachments first used in subsequent subpasses. */
    cmd->current_render_pass.clear_value_count =
        (pBeginInfo->clearValueCount > 16) ? 16 : pBeginInfo->clearValueCount;
    if (pBeginInfo->pClearValues && cmd->current_render_pass.clear_value_count > 0) {
        memcpy(cmd->current_render_pass.clear_values,
               pBeginInfo->pClearValues,
               cmd->current_render_pass.clear_value_count * sizeof(VkClearValue));
    }

    /* Set scissor to render area first (needed for draw-based clears) */
    sceGnmDrawCmdSetScreenScissor(&cmd->gnm_cmd,
        pBeginInfo->renderArea.offset.x,
        pBeginInfo->renderArea.offset.y,
        pBeginInfo->renderArea.offset.x + pBeginInfo->renderArea.extent.width,
        pBeginInfo->renderArea.offset.y + pBeginInfo->renderArea.extent.height);

    /* Bind render targets based on the current subpass description.
     * This correctly maps framebuffer attachments to RT slots via
     * pColorAttachments[j].attachment, and binds the depth/stencil
     * attachment via pDepthStencilAttachment. */
    vk_ps4_bind_subpass_targets(cmd);

    /* If any framebuffer attachment is a swapchain image, emit
     * WaitUntilSafeForRendering so the GPU waits until the display
     * engine has finished reading the buffer before we render to it.
     * This prevents tearing and GPU/display races on swapchain images. */
    for (uint32_t i = 0; i < fb->attachment_count; i++) {
        VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, i);
        if (view && view->image && view->image->is_swapchain_image) {
            sceGnmDrawCmdWaitUntilSafeForRendering(
                &cmd->gnm_cmd,
                view->image->video_out_handle,
                view->image->swapchain_buffer_index
            );
        }
    }

    vk_ps4_clear_subpass_loads(cmd);

    /* Re-bind subpass targets after load-op clears.  Draw-based clears
     * (vk_ps4_clear_color_draw) bind the cleared RT at slot 0 and clobber
     * blend/scissor state.  Re-binding restores the correct RT-to-slot
     * mapping from the subpass description.  Pipeline state (PS, VS,
     * blend, scissor) is restored when the user calls CmdBindPipeline. */
    vk_ps4_bind_subpass_targets(cmd);
    vk_ps4_rebind_pipeline_state(cmd);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdEndRenderPass(VkCommandBuffer commandBuffer) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* Defensive reset. vk_ps4_clear_depth_draw() already disables the sticky
     * clear bits immediately after each meta draw. */
    GnmDbRenderControl db_ctrl;
    memset(&db_ctrl, 0, sizeof(db_ctrl));
    db_ctrl.depthclearenable = 0;
    db_ctrl.stencilclearenable = 0;
    sceGnmDrawCmdSetDbRenderControl(&cmd->gnm_cmd, &db_ctrl);

    /* DIAGNOSTIC (black-screen investigation): every EOP flush in this ICD
     * uses the generic CACHE_FLUSH_AND_INV_TS_EVENT, and none ever emits
     * FLUSH_AND_INV_CB_DATA_TS - a distinct event type this header defines
     * specifically for flushing/invalidating color-buffer data, and which
     * is otherwise unused anywhere in the codebase. The presented buffer's
     * pixels never change across 1400+ frames of real draws while CPU/DMA
     * writes (FillMemory) always land, which is consistent with CB writes
     * completing on-GPU but never being written back to the memory that
     * flip/scanout and this host-side pixel dump actually read. Emit it
     * before the general flush, in case the two are not redundant here. */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_FLUSH_AND_INV_CB_DATA_TS, 0,
        GNM_DATA_SEL_DISCARD, 0);

    /* Emit EOP event to signal completion */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0,
        GNM_DATA_SEL_DISCARD, 0);

    cmd->current_render_pass.pass = NULL;
    cmd->current_render_pass.framebuffer = NULL;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) {
    (void)contents;
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* Advance to the next subpass and re-bind render targets.
     * Bounds check: if we're already at the last subpass, do nothing. */
    if (!cmd->current_render_pass.pass) return;
    VkPs4RenderPass *rp = cmd->current_render_pass.pass;
    uint32_t next = cmd->current_render_pass.current_subpass + 1;
    if (next >= rp->subpass_count) return;

    cmd->current_render_pass.current_subpass = next;
    vk_ps4_bind_subpass_targets(cmd);

    vk_ps4_clear_subpass_loads(cmd);

    /* Re-bind subpass targets after load-op clears.  Draw-based clears
     * bind the cleared RT at slot 0 and clobber blend/scissor state.
     * Re-binding restores the correct RT-to-slot mapping. */
    vk_ps4_bind_subpass_targets(cmd);
    vk_ps4_rebind_pipeline_state(cmd);
}

/* === Barriers === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                          VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                          uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                          uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                          uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* Conservative release/acquire barrier for the serial-safe backend.
     * EventWriteEop publishes CB/DB/CP-DMA writes; the following acquire
     * stalls graphics and invalidates every attachment target before a
     * newly uploaded texture or depth surface is consumed.  Layouts remain
     * software bookkeeping, but memory visibility is no longer bookkeeping
     * only. */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0,
        GNM_DATA_SEL_DISCARD, 0);
    const GnmAcquireTargetFlags acquire_all = (GnmAcquireTargetFlags)(
        GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_CB1 |
        GNM_ACQUIRE_TARGET_CB2 | GNM_ACQUIRE_TARGET_CB3 |
        GNM_ACQUIRE_TARGET_CB4 | GNM_ACQUIRE_TARGET_CB5 |
        GNM_ACQUIRE_TARGET_CB6 | GNM_ACQUIRE_TARGET_CB7 |
        GNM_ACQUIRE_TARGET_DB
    );
    sceGnmDrawCmdWaitGraphicsWrite(&cmd->gnm_cmd, acquire_all);

    /* Track image layout transitions */
    for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
        const VkImageMemoryBarrier *b = &pImageMemoryBarriers[i];
        if (b->oldLayout != b->newLayout) {
            VkPs4Image *img = (VkPs4Image *)b->image;
            if (img) img->layout = b->newLayout;
        }
    }

    (void)srcStageMask;
    (void)dstStageMask;
    (void)dependencyFlags;
    (void)memoryBarrierCount;
    (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount;
    (void)pBufferMemoryBarriers;
}

/* === Event commands === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) {
    if (!commandBuffer || !event) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Event *ev = (VkPs4Event *)event;

    /* Emit an EOP event write to signal the GPU side.
     * The CPU-side signaled flag is also set for GetEventStatus polling.
     * KNOWN LIMITATION: Without a GPU-visible memory location for the event,
     * we can't truly signal a GPU event. This sets the CPU flag and emits
     * a cache flush as a side effect. */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0,
        GNM_DATA_SEL_DISCARD, 0);
    ev->signaled = true;
    (void)stageMask;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event, VkPipelineStageFlags stageMask) {
    if (!commandBuffer || !event) return;
    VkPs4Event *ev = (VkPs4Event *)event;

    /* Reset the CPU-side flag. On the GPU side, there's nothing to do
     * since we don't have a GPU-visible event memory location. */
    ev->signaled = false;
    (void)stageMask;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent *pEvents,
                     VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                     uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
                     uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                     uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers) {
    if (!commandBuffer) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;

    /* Emit a cache flush + wait as a full barrier.
     * KNOWN LIMITATION: Without GPU-visible event memory, we can't wait
     * on specific events. We emit a full pipeline stall instead, which
     * is over-synchronized but safe. */
    sceGnmDrawCmdEventWriteEop(&cmd->gnm_cmd,
        GNM_CACHE_FLUSH_AND_INV_TS_EVENT, 0,
        GNM_DATA_SEL_DISCARD, 0);
    sceGnmDrawCmdWaitGraphicsWrite(&cmd->gnm_cmd, 0);

    /* Track image layout transitions */
    for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
        const VkImageMemoryBarrier *b = &pImageMemoryBarriers[i];
        if (b->oldLayout != b->newLayout) {
            VkPs4Image *img = (VkPs4Image *)b->image;
            if (img) img->layout = b->newLayout;
        }
    }

    (void)eventCount; (void)pEvents;
    (void)srcStageMask; (void)dstStageMask;
    (void)memoryBarrierCount; (void)pMemoryBarriers;
    (void)bufferMemoryBarrierCount; (void)pBufferMemoryBarriers;
}

/* === Clear commands === */

/* Fill one complete, padded subresource. For <=32-bit uncompressed pixels
 * the uniform pixel pattern is invariant under tile swizzling; the address
 * library selects the real mip/slice allocation, never width*height guesses. */
static bool vk_ps4_clear_uniform_format(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32: case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8_SRGB:
    case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_R16G16_UNORM: case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SFLOAT:
        return true;
    default: return false;
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                          const VkClearColorValue *pColor, uint32_t rangeCount,
                          const VkImageSubresourceRange *pRanges) {
    (void)imageLayout;
    if (!commandBuffer || !image || !pColor || !pRanges) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Image *img = (VkPs4Image *)image;
    VkClearValue value = {0}; value.color = *pColor;
    if (!img->memory || !img->memory->gnm_mem.mapped || img->is_depth_target) {
        vk_ps4_command_fail(cmd, "color clear on unbound/non-color image"); return;
    }
    for (uint32_t r = 0; r < rangeCount; ++r) {
        const VkImageSubresourceRange *range = &pRanges[r];
        uint32_t levels, layers;
        if (range->aspectMask != VK_IMAGE_ASPECT_COLOR_BIT ||
            !vk_ps4_resolve_range(range->baseMipLevel, range->levelCount, img->create_info.mipLevels, &levels) ||
            !vk_ps4_resolve_range(range->baseArrayLayer, range->layerCount, img->create_info.arrayLayers, &layers)) {
            vk_ps4_command_fail(cmd, "color clear subresource out of range"); return;
        }
        if (img->is_render_target) {
            const VkRect2D rect = {{0,0}, {img->create_info.extent.width, img->create_info.extent.height}};
            vk_ps4_clear_region(cmd, img, VK_IMAGE_ASPECT_COLOR_BIT, &value, &rect,
                                range->baseArrayLayer, layers);
            continue;
        }
        if (!vk_ps4_clear_uniform_format(img->create_info.format)) {
            vk_ps4_command_fail(cmd, "texture clear pixel format unsupported"); return;
        }
        const uint32_t packed = vk_ps4_pack_clear_val_32(img->create_info.format, pColor);
        const GpaTextureInfo info = sceGnmTexBuildInfo(&img->gnm_texture);
        for (uint32_t mip = range->baseMipLevel; mip < range->baseMipLevel + levels; ++mip) {
            for (uint32_t layer = range->baseArrayLayer; layer < range->baseArrayLayer + layers; ++layer) {
                uint64_t size = 0, offset = 0;
                if (sceGpaComputeSurfaceSizeOffset(&size, &offset, &info, mip, layer) != GPA_ERR_OK ||
                    img->memory_offset > img->memory->size ||
                    offset > img->memory->size - img->memory_offset ||
                    size > img->memory->size - img->memory_offset - offset ||
                    (size & 3) || (offset & 3)) {
                    vk_ps4_command_fail(cmd, "texture clear address-library bounds/alignment"); return;
                }
                uint64_t address = (uint64_t)img->memory->gnm_mem.mapped + img->memory_offset + offset;
                while (size) {
                    const uint32_t chunk = size > 0xfffffffcu ? 0xfffffffcu : (uint32_t)size;
                    sceGnmDrawCmdFillMemory(&cmd->gnm_cmd, address, chunk, packed);
                    address += chunk; size -= chunk;
                }
            }
        }
    }
    vk_ps4_bind_subpass_targets(cmd);
    vk_ps4_rebind_pipeline_state(cmd);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
                                 const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
                                 const VkImageSubresourceRange *pRanges) {
    (void)imageLayout;
    if (!commandBuffer || !image || !pDepthStencil || !pRanges) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Image *img = (VkPs4Image *)image;
    VkClearValue value = {0}; value.depthStencil = *pDepthStencil;
    const VkRect2D rect = {{0,0}, {img->create_info.extent.width, img->create_info.extent.height}};
    for (uint32_t r = 0; r < rangeCount; ++r) {
        const VkImageSubresourceRange *range = &pRanges[r];
        uint32_t levels, layers;
        if (!vk_ps4_resolve_range(range->baseMipLevel, range->levelCount, img->create_info.mipLevels, &levels) ||
            !vk_ps4_resolve_range(range->baseArrayLayer, range->layerCount, img->create_info.arrayLayers, &layers) ||
            range->baseMipLevel != 0 || levels != 1 ||
            (range->aspectMask & ~(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) {
            vk_ps4_command_fail(cmd, "depth/stencil clear subresource unsupported"); return;
        }
        vk_ps4_clear_region(cmd, img, range->aspectMask, &value, &rect, range->baseArrayLayer, layers);
    }
    vk_ps4_bind_subpass_targets(cmd);
    vk_ps4_rebind_pipeline_state(cmd);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                           const VkClearAttachment *pAttachments, uint32_t rectCount,
                           const VkClearRect *pRects) {
    if (!commandBuffer || !pAttachments || !pRects || !rectCount) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    VkPs4Framebuffer *fb = cmd->current_render_pass.framebuffer;
    if (!fb) return;
    for (uint32_t a = 0; a < attachmentCount; ++a) {
        const VkClearAttachment *att = &pAttachments[a];
        const uint32_t index = att->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT
            ? vk_ps4_subpass_color_attachment(cmd, att->colorAttachment)
            : vk_ps4_subpass_depth_attachment(cmd);
        if (index == VK_ATTACHMENT_UNUSED) continue;
        VkPs4ImageView *view = vk_ps4_get_attachment_view(cmd, index);
        if (!view || !view->image) {
            vk_ps4_command_fail(cmd, "clear attachment missing view"); return;
        }
        for (uint32_t r = 0; r < rectCount; ++r) {
            const VkClearRect *rect = &pRects[r];
            uint32_t layers;
            if (!vk_ps4_resolve_range(rect->baseArrayLayer, rect->layerCount, fb->layers, &layers) ||
                rect->baseArrayLayer + layers > view->create_info.subresourceRange.layerCount) {
                vk_ps4_command_fail(cmd, "clear attachment framebuffer layers out of range"); return;
            }
            vk_ps4_clear_region(cmd, view->image, att->aspectMask, &att->clearValue, &rect->rect,
                view->create_info.subresourceRange.baseArrayLayer + rect->baseArrayLayer, layers);
        }
    }
    vk_ps4_bind_subpass_targets(cmd);
    vk_ps4_rebind_pipeline_state(cmd);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                        VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                        const void *pValues) {
    if (!commandBuffer || !pValues || size == 0) return;
    VkPs4CommandBuffer *cmd = (VkPs4CommandBuffer *)commandBuffer;
    if (offset > 128 || size > 128 - offset || (offset & 3) || (size & 3)) {
        vk_ps4_command_fail(cmd, "push constant range exceeds 128-byte driver limit"); return;
    }
    /* Recorded whether or not a pipeline is bound yet, and written to the
     * user-data registers of whichever pipeline the next draw or dispatch
     * runs (vk_ps4_flush_push_constants). */
    if (stageFlags & VK_SHADER_STAGE_VERTEX_BIT) {
        cmd->graphics_push_dirty[0] |= !cmd->graphics_push_valid[0] ||
            memcmp((char *)cmd->graphics_push_constants[0] + offset, pValues, size) != 0;
        memmove((char *)cmd->graphics_push_constants[0] + offset, pValues, size);
        cmd->graphics_push_valid[0] = true;
    }
    if (stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT) {
        cmd->graphics_push_dirty[1] |= !cmd->graphics_push_valid[1] ||
            memcmp((char *)cmd->graphics_push_constants[1] + offset, pValues, size) != 0;
        memmove((char *)cmd->graphics_push_constants[1] + offset, pValues, size);
        cmd->graphics_push_valid[1] = true;
    }
    if (stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
        cmd->compute_push_dirty |= !cmd->compute_push_valid ||
            memcmp((char *)cmd->compute_push_constants + offset, pValues, size) != 0;
        memmove((char *)cmd->compute_push_constants + offset, pValues, size);
        cmd->compute_push_valid = true;
    }
    (void)layout;
}

/* Query commands moved to vk_ps4_query.c */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                           uint32_t commandBufferCount,
                           const VkCommandBuffer *pCommandBuffers) {
    if (!commandBuffer || !pCommandBuffers || commandBufferCount == 0) return;
    VkPs4CommandBuffer *primary = (VkPs4CommandBuffer *)commandBuffer;
    if (primary->recording_error != VK_SUCCESS) return;

    /* CmdExecuteCommands copies PM4 data from each secondary command buffer
     * into the primary's PM4 stream. This is the simplest correct approach —
     * the GPU sees a single contiguous PM4 buffer at submit time.
     *
     * KNOWN LIMITATION: We don't handle VkCommandBufferInheritanceInfo yet.
     * Secondary buffers recorded with VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT
     * should inherit the primary's render pass / framebuffer, but we currently
     * ignore inheritance info and just copy the raw PM4. */
    for (uint32_t i = 0; i < commandBufferCount; i++) {
        VkPs4CommandBuffer *secondary = (VkPs4CommandBuffer *)pCommandBuffers[i];
        if (!secondary) continue;
        if (secondary->recording_error != VK_SUCCESS) {
            primary->recording_error = secondary->recording_error;
            return;
        }
        if (secondary->is_recording ||
            secondary->level != VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
            vk_ps4_command_fail(primary, "secondary command buffer is not executable");
            return;
        }

        /* Calculate the size of PM4 data in the secondary buffer */
        uint32_t *src_begin = secondary->gnm_cmd.beginptr;
        uint32_t *src_end = secondary->gnm_cmd.cmdptr;
        uint32_t src_dwords = (uint32_t)(src_end - src_begin);
        if (src_dwords == 0) continue;

        /* Check that the primary has enough space */
        uint32_t *dst_begin = primary->gnm_cmd.cmdptr;
        uint32_t *dst_end = primary->gnm_cmd.endptr;
        uint32_t dst_avail = (uint32_t)(dst_end - dst_begin);
        if (dst_avail < src_dwords) {
            /* A missing secondary is missing visible geometry. Reject the
             * entire primary; never silently submit only the draws that fit. */
            if (!vk_ps4_command_overflow(&primary->gnm_cmd, src_dwords, primary)) return;
            dst_begin = primary->gnm_cmd.cmdptr;
        }

        /* Copy the PM4 data from secondary to primary */
        memcpy(dst_begin, src_begin, src_dwords * sizeof(uint32_t));
        primary->gnm_cmd.cmdptr += src_dwords;
        primary->pm4_used += src_dwords;
        primary->compute_dispatch_count += secondary->compute_dispatch_count;
    }
    /* Vulkan requires re-binding state after executing secondaries. Do not
     * let immutable-table reuse mistake a secondary's hardware state for
     * the primary's previous bindings. */
    primary->current_pipeline = NULL;
    primary->vertex_table_pipeline = NULL;
}
