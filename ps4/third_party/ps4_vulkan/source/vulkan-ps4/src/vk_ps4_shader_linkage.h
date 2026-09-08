#ifndef VK_PS4_SHADER_LINKAGE_H
#define VK_PS4_SHADER_LINKAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <gnm_shader.h>
#include <pm4/amdgfxregs.h>

/* PSBC writes Mesa varying-slot IDs into the GNM semantic table. PointCoord
 * is a rasterizer-generated interpolator, not a VS PARAM export. Keep this
 * value tied to the bundled compiler's VARYING_SLOT_PNTC (shader_enums.h). */
#define VK_PS4_SEMANTIC_POINT_COORD 25u

static inline bool vk_ps4_ps_input_control(
    const GnmVertexExportSemantic *exports, uint32_t export_count,
    const GnmPixelInputSemantic *input, uint32_t *control)
{
    if (!input || !control || (export_count && !exports))
        return false;
    if (input->semantic == VK_PS4_SEMANTIC_POINT_COORD) {
        *control = S_028644_OFFSET(0x20) | S_028644_PT_SPRITE_TEX(1);
        return true;
    }
    for (uint32_t v = 0; v < export_count; ++v) {
        if (exports[v].semantic != input->semantic)
            continue;
        /* A normal PARAM export is 0..31. Undefined compiler offsets must
         * never alias a valid interpolator after register truncation. */
        if (exports[v].outindex >= 32)
            return false;
        *control = S_028644_OFFSET(exports[v].outindex) |
            S_028644_DEFAULT_VAL(input->defaultvalue) |
            S_028644_FLAT_SHADE(input->isflatshaded ? 1u : 0u);
        return true;
    }
    return false;
}

static inline bool vk_ps4_ps_uses_point_coord(
    const GnmPixelInputSemantic *inputs, uint32_t input_count)
{
    for (uint32_t p = 0; p < input_count; ++p)
        if (inputs[p].semantic == VK_PS4_SEMANTIC_POINT_COORD)
            return true;
    return false;
}

static inline uint32_t vk_ps4_ps_interpolator_control(bool point_coord)
{
    /* Vulkan PointCoord has its origin in the upper left: top T = 0.
     * Keep the global flat-interpolation gate enabled: individual inputs
     * select flat/smooth in SPI_PS_INPUT_CNTL_n. Ordinary/depth-only binds
     * disable only sprite replacement, not flat inputs in a later M2 draw.
     * This matches Mesa's GCN state dump (freedesktop attachment 122212). */
    const uint32_t base = S_0286D4_FLAT_SHADE_ENA(1);
    return base | (point_coord ?
        S_0286D4_PNT_SPRITE_ENA(1) |
        S_0286D4_PNT_SPRITE_OVRD_X(V_0286D4_SPI_PNT_SPRITE_SEL_S) |
        S_0286D4_PNT_SPRITE_OVRD_Y(V_0286D4_SPI_PNT_SPRITE_SEL_T) |
        S_0286D4_PNT_SPRITE_OVRD_Z(V_0286D4_SPI_PNT_SPRITE_SEL_0) |
        S_0286D4_PNT_SPRITE_OVRD_W(V_0286D4_SPI_PNT_SPRITE_SEL_1) |
        S_0286D4_PNT_SPRITE_TOP_1(0) : 0);
}

#endif
