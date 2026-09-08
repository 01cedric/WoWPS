/*
 * vk_ps4_format.c — VkFormat to GnmDataFormat mapping.
 *
 * Maps Vulkan format enums to the equivalent GNM (GCN) data format.
 * Not all VkFormat values have a direct GNM equivalent — those return
 * GNM_FMT_INVALID and the caller must reject them.
 */

#include "vk_ps4_internal.h"

#include <string.h>

/* === Format mapping table === */
typedef struct {
    VkFormat vk_format;
    GnmDataFormat gnm_format;
    VkFormatFeatureFlags features;
} VkPs4FormatEntry;

/* Helper to build GnmDataFormat from components */
#define FMT(surfacefmt, chantype, cx, cy, cz, cw) \
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_##surfacefmt, GNM_IMG_NUM_FORMAT_##chantype, GNM_CHAN_##cx, GNM_CHAN_##cy, GNM_CHAN_##cz, GNM_CHAN_##cw)

/* Common feature flags */
#define FMT_TRANSFER (VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
#define FMT_COLOR_RT (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
#define FMT_SAMPLED  (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)
#define FMT_DEPTH    (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
#define FMT_BLIT_SRC (VK_FORMAT_FEATURE_BLIT_SRC_BIT)
#define FMT_BLIT_DST (VK_FORMAT_FEATURE_BLIT_DST_BIT)
#define FMT_ALL      (FMT_SAMPLED | FMT_COLOR_RT | FMT_BLIT_SRC | FMT_BLIT_DST | FMT_TRANSFER)
#define FMT_TEX_ONLY (FMT_SAMPLED | FMT_BLIT_SRC | FMT_TRANSFER)

static const VkPs4FormatEntry g_format_table[] = {
    /* === 8-bit === */
    { VK_FORMAT_R8_UNORM,       FMT(8, UNORM, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_ALL },
    { VK_FORMAT_R8_SNORM,       FMT(8, SNORM, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R8_UINT,        FMT(8, UINT,  X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R8_SINT,        FMT(8, SINT,  X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R8_SRGB,        FMT(8, SRGB,  X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },

    /* === 8-bit BGRA (PS4 VideoOut native) === */
    { VK_FORMAT_B8G8R8A8_UNORM, FMT(8_8_8_8, UNORM, Z, Y, X, W), FMT_ALL },
    { VK_FORMAT_B8G8R8A8_SRGB,  FMT(8_8_8_8, SRGB,  Z, Y, X, W), FMT_ALL },
    { VK_FORMAT_R8G8B8A8_UNORM, FMT(8_8_8_8, UNORM, X, Y, Z, W), FMT_ALL },
    { VK_FORMAT_R8G8B8A8_SRGB,  FMT(8_8_8_8, SRGB,  X, Y, Z, W), FMT_ALL },
    { VK_FORMAT_R8G8B8A8_SNORM, FMT(8_8_8_8, SNORM, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_R8G8B8A8_UINT,  FMT(8_8_8_8, UINT,  X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_R8G8B8A8_SINT,  FMT(8_8_8_8, SINT,  X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_A8B8G8R8_UNORM_PACK32, FMT(8_8_8_8, UNORM, X, Y, Z, W), FMT_ALL },
    { VK_FORMAT_A8B8G8R8_SRGB_PACK32,  FMT(8_8_8_8, SRGB,  X, Y, Z, W), FMT_ALL },

    /* === A8 (alpha only) === */
    { VK_FORMAT_R8G8_UNORM,     FMT(8_8, UNORM, X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R8G8_SNORM,     FMT(8_8, SNORM, X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R8G8_UINT,      FMT(8_8, UINT,  X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R8G8_SINT,      FMT(8_8, SINT,  X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },

    /* === 16-bit === */
    { VK_FORMAT_R16_UNORM,      FMT(16, UNORM, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R16_SNORM,      FMT(16, SNORM, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R16_UINT,       FMT(16, UINT,  X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R16_SINT,       FMT(16, SINT,  X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R16_SFLOAT,     FMT(16, FLOAT, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY | FMT_COLOR_RT },

    { VK_FORMAT_R16G16_UNORM,   FMT(16_16, UNORM, X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R16G16_SNORM,   FMT(16_16, SNORM, X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R16G16_UINT,    FMT(16_16, UINT,  X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R16G16_SINT,    FMT(16_16, SINT,  X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R16G16_SFLOAT,  FMT(16_16, FLOAT, X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY | FMT_COLOR_RT },

    { VK_FORMAT_R16G16B16A16_UNORM,  FMT(16_16_16_16, UNORM, X, Y, Z, W), FMT_ALL },
    { VK_FORMAT_R16G16B16A16_SNORM,  FMT(16_16_16_16, SNORM, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_R16G16B16A16_UINT,   FMT(16_16_16_16, UINT,  X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_R16G16B16A16_SINT,   FMT(16_16_16_16, SINT,  X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_R16G16B16A16_SFLOAT, FMT(16_16_16_16, FLOAT, X, Y, Z, W), FMT_ALL },

    /* === 32-bit === */
    { VK_FORMAT_R32_UINT,       FMT(32, UINT,  X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R32_SINT,       FMT(32, SINT,  X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R32_SFLOAT,     FMT(32, FLOAT, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_ALL },

    { VK_FORMAT_R32G32_UINT,    FMT(32_32, UINT,  X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R32G32_SINT,    FMT(32_32, SINT,  X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R32G32_SFLOAT,  FMT(32_32, FLOAT, X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY | FMT_COLOR_RT },

    { VK_FORMAT_R32G32B32_UINT,    FMT(32_32_32, UINT,  X, Y, Z, CONSTANT0), FMT_TEX_ONLY },
    { VK_FORMAT_R32G32B32_SINT,    FMT(32_32_32, SINT,  X, Y, Z, CONSTANT0), FMT_TEX_ONLY },
    { VK_FORMAT_R32G32B32_SFLOAT,  FMT(32_32_32, FLOAT, X, Y, Z, CONSTANT1), FMT_TEX_ONLY },

    { VK_FORMAT_R32G32B32A32_UINT,   FMT(32_32_32_32, UINT,  X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_R32G32B32A32_SINT,   FMT(32_32_32_32, SINT,  X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_R32G32B32A32_SFLOAT, FMT(32_32_32_32, FLOAT, X, Y, Z, W), FMT_ALL },

    /* === 565 / 5551 / 4444 === */
    { VK_FORMAT_R5G6B5_UNORM_PACK16,   FMT(5_6_5, UNORM, X, Y, Z, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_B5G6R5_UNORM_PACK16,   FMT(5_6_5, UNORM, Z, Y, X, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_R5G5B5A1_UNORM_PACK16, FMT(1_5_5_5, UNORM, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_A1R5G5B5_UNORM_PACK16, FMT(1_5_5_5, UNORM, Z, Y, X, W), FMT_TEX_ONLY },
    { VK_FORMAT_B5G5R5A1_UNORM_PACK16, FMT(1_5_5_5, UNORM, Z, Y, X, W), FMT_TEX_ONLY },
    { VK_FORMAT_R4G4B4A4_UNORM_PACK16, FMT(4_4_4_4, UNORM, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_B4G4R4A4_UNORM_PACK16, FMT(4_4_4_4, UNORM, Z, Y, X, W), FMT_TEX_ONLY },

    /* === 10/11/11 === */
    { VK_FORMAT_B10G11R11_UFLOAT_PACK32, FMT(10_11_11, FLOAT, X, Y, Z, CONSTANT1), FMT_TEX_ONLY | FMT_COLOR_RT },

    /* === 2_10_10_10 === */
    { VK_FORMAT_A2B10G10R10_UNORM_PACK32, FMT(2_10_10_10, UNORM, X, Y, Z, W), FMT_ALL },
    { VK_FORMAT_A2B10G10R10_UINT_PACK32,  FMT(2_10_10_10, UINT,  X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_A2R10G10B10_UNORM_PACK32, FMT(2_10_10_10, UNORM, Z, Y, X, W), FMT_ALL },
    { VK_FORMAT_A2R10G10B10_UINT_PACK32,  FMT(2_10_10_10, UINT,  Z, Y, X, W), FMT_TEX_ONLY },

    /* === Depth/stencil === */
    /* GNM uses separate depth/stencil formats. VkFormat combined formats
     * map to GNM depth render targets with stencil. */
    { VK_FORMAT_D16_UNORM,          FMT(16, UNORM, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_DEPTH },
    { VK_FORMAT_D32_SFLOAT,         FMT(32, FLOAT, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_DEPTH },
    { VK_FORMAT_D24_UNORM_S8_UINT,  FMT(24_8, UNORM, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_DEPTH },
    { VK_FORMAT_D32_SFLOAT_S8_UINT, FMT(32, FLOAT, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_DEPTH },

    /* === BC compressed === */
    { VK_FORMAT_BC1_RGB_UNORM_BLOCK, FMT(BC1, UNORM, X, Y, Z, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_BC1_RGBA_UNORM_BLOCK, FMT(BC1, UNORM, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_BC1_RGB_SRGB_BLOCK,  FMT(BC1, SRGB, X, Y, Z, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_BC1_RGBA_SRGB_BLOCK, FMT(BC1, SRGB, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_BC2_UNORM_BLOCK,     FMT(BC2, UNORM, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_BC2_SRGB_BLOCK,      FMT(BC2, SRGB, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_BC3_UNORM_BLOCK,     FMT(BC3, UNORM, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_BC3_SRGB_BLOCK,      FMT(BC3, SRGB, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_BC4_UNORM_BLOCK,     FMT(BC4, UNORM, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_BC4_SNORM_BLOCK,     FMT(BC4, SNORM, X, CONSTANT0, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_BC5_UNORM_BLOCK,     FMT(BC5, UNORM, X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_BC5_SNORM_BLOCK,     FMT(BC5, SNORM, X, Y, CONSTANT0, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_BC6H_UFLOAT_BLOCK,   FMT(BC6, UNORM, X, Y, Z, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_BC6H_SFLOAT_BLOCK,   FMT(BC6, SNORM, X, Y, Z, CONSTANT1), FMT_TEX_ONLY },
    { VK_FORMAT_BC7_UNORM_BLOCK,     FMT(BC7, UNORM, X, Y, Z, W), FMT_TEX_ONLY },
    { VK_FORMAT_BC7_SRGB_BLOCK,      FMT(BC7, SRGB, X, Y, Z, W), FMT_TEX_ONLY },
};

static const size_t g_format_table_count =
    sizeof(g_format_table) / sizeof(g_format_table[0]);

GnmDataFormat vk_ps4_vk_format_to_gnm(VkFormat format) {
    for (size_t i = 0; i < g_format_table_count; i++) {
        if (g_format_table[i].vk_format == format) {
            return g_format_table[i].gnm_format;
        }
    }
    return GNM_FMT_INVALID;
}

VkFormatProperties vk_ps4_format_properties(VkFormat format) {
    for (size_t i = 0; i < g_format_table_count; i++) {
        if (g_format_table[i].vk_format == format) {
            VkFormatProperties props = {
                .linearTilingFeatures = g_format_table[i].features,
                .optimalTilingFeatures = g_format_table[i].features,
                .bufferFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT,
            };
            props.linearTilingFeatures &= ~(VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT);
            props.optimalTilingFeatures &= ~(VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT);
            /* Only advertise texel buffer features for formats that have
             * a valid GnmBufferFormat equivalent. SRGB and BC compressed
             * formats have no buffer representation — sceGnmBufSetFormat
             * rejects them silently. */
            GnmDataFormat gnm = g_format_table[i].gnm_format;
            if (vk_ps4_gnm_format_is_buffer_compatible(gnm)) {
                props.bufferFeatures |=
                    VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
                    VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
            }
            return props;
        }
    }
    /* Unsupported format */
    VkFormatProperties empty = {0};
    return empty;
}

bool vk_ps4_gnm_format_is_buffer_compatible(GnmDataFormat fmt) {
    /* GnmBufferFormat supports: 8, 16, 8_8, 32, 16_16, 10_11_11, 11_11_10,
     * 10_10_10_2, 2_10_10_10, 8_8_8_8, 32_32, 16_16_16_16, 32_32_32, 32_32_32_32.
     * GnmBufNumFormat supports: UNORM, SNORM, USCALED, SSCALED, UINT, SINT, SNORM_OGL, FLOAT.
     * Notably missing: SRGB (no buffer SRGB), BC1-7 (no compressed buffers),
     * 16_16_16 (3-channel not in buffer format list). */
    switch (fmt.surfacefmt) {
    case GNM_IMG_DATA_FORMAT_8:
    case GNM_IMG_DATA_FORMAT_16:
    case GNM_IMG_DATA_FORMAT_8_8:
    case GNM_IMG_DATA_FORMAT_32:
    case GNM_IMG_DATA_FORMAT_16_16:
    case GNM_IMG_DATA_FORMAT_10_11_11:
    case GNM_IMG_DATA_FORMAT_11_11_10:
    case GNM_IMG_DATA_FORMAT_10_10_10_2:
    case GNM_IMG_DATA_FORMAT_2_10_10_10:
    case GNM_IMG_DATA_FORMAT_8_8_8_8:
    case GNM_IMG_DATA_FORMAT_32_32:
    case GNM_IMG_DATA_FORMAT_16_16_16_16:
    case GNM_IMG_DATA_FORMAT_32_32_32:
    case GNM_IMG_DATA_FORMAT_32_32_32_32:
        break;  /* valid buffer data format */
    default:
        return false;  /* BC1-7, 1, 1_REVERSED, etc. not buffer-compatible */
    }
    switch (fmt.chantype) {
    case GNM_IMG_NUM_FORMAT_UNORM:
    case GNM_IMG_NUM_FORMAT_SNORM:
    case GNM_IMG_NUM_FORMAT_USCALED:
    case GNM_IMG_NUM_FORMAT_SSCALED:
    case GNM_IMG_NUM_FORMAT_UINT:
    case GNM_IMG_NUM_FORMAT_SINT:
    case GNM_IMG_NUM_FORMAT_FLOAT:
        return true;
    default:
        return false;  /* SRGB, SNORM_OGL (IMG-only) not buffer-compatible */
    }
}

GnmDataFormat vk_ps4_vk_format_to_gnm_buffer(VkFormat format) {
    GnmDataFormat img_fmt = vk_ps4_vk_format_to_gnm(format);
    if (!vk_ps4_gnm_format_is_buffer_compatible(img_fmt)) {
        return GNM_FMT_INVALID;
    }
    /* The IMG and BUF format enums share the same numeric values for
     * the common subset. The channel type (num format) also shares values
     * for UNORM/SNORM/USCALED/SSCALED/UINT/SINT/FLOAT.
     * So the GnmDataFormat can be used directly with sceGnmBufSetFormat. */
    return img_fmt;
}
