#ifndef _GNM_STRINGS_H_
#define _GNM_STRINGS_H_

#include "gnm_shaderbinary.h"
#include "gnm_shader.h"
#include "gnm_types.h"

static inline const char* gnmStrPipeConfig(GnmPipeConfig cfg) {
	switch (cfg) {
	case GNM_ADDR_SURF_P8_32x32_8x16:
		return "P8_32x32_8x16";
	case GNM_ADDR_SURF_P8_32x32_16x16:
		return "P8_32x32_16x16";
	default:
		return "Unknown config";
	}
}

static inline const char* gnmStrSampleSplit(GnmSampleSplit split) {
	switch (split) {
	case GNM_ADDR_SAMPLE_SPLIT_1:
		return "1";
	case GNM_ADDR_SAMPLE_SPLIT_2:
		return "2";
	case GNM_ADDR_SAMPLE_SPLIT_4:
		return "4";
	case GNM_ADDR_SAMPLE_SPLIT_8:
		return "8";
	default:
		return "Unknown split";
	}
}

static inline const char* gnmStrTileSplit(GnmTileSplit split) {
	switch (split) {
	case GNM_SURF_TILE_SPLIT_64B:
		return "64B";
	case GNM_SURF_TILE_SPLIT_128B:
		return "128B";
	case GNM_SURF_TILE_SPLIT_256B:
		return "256B";
	case GNM_SURF_TILE_SPLIT_512B:
		return "512B";
	case GNM_SURF_TILE_SPLIT_1KB:
		return "1KB";
	case GNM_SURF_TILE_SPLIT_2KB:
		return "2KB";
	case GNM_SURF_TILE_SPLIT_4KB:
		return "4KB";
	default:
		return "Unknown split";
	}
}

static inline const char* gnmStrSurfaceFormat(GnmImageFormat fmt) {
	switch (fmt) {
	case GNM_IMG_DATA_FORMAT_INVALID:
		return "Invalid";
	case GNM_IMG_DATA_FORMAT_8:
		return "8";
	case GNM_IMG_DATA_FORMAT_16:
		return "16";
	case GNM_IMG_DATA_FORMAT_8_8:
		return "8_8";
	case GNM_IMG_DATA_FORMAT_32:
		return "32";
	case GNM_IMG_DATA_FORMAT_16_16:
		return "16_16";
	case GNM_IMG_DATA_FORMAT_10_11_11:
		return "10_11_11";
	case GNM_IMG_DATA_FORMAT_11_11_10:
		return "11_11_10";
	case GNM_IMG_DATA_FORMAT_10_10_10_2:
		return "10_10_10_2";
	case GNM_IMG_DATA_FORMAT_2_10_10_10:
		return "2_10_10_10";
	case GNM_IMG_DATA_FORMAT_8_8_8_8:
		return "8_8_8_8";
	case GNM_IMG_DATA_FORMAT_32_32:
		return "32_32";
	case GNM_IMG_DATA_FORMAT_16_16_16_16:
		return "16_16_16_16";
	case GNM_IMG_DATA_FORMAT_32_32_32:
		return "32_32_32";
	case GNM_IMG_DATA_FORMAT_32_32_32_32:
		return "32_32_32_32";
	case GNM_IMG_DATA_FORMAT_5_6_5:
		return "5_6_5";
	case GNM_IMG_DATA_FORMAT_1_5_5_5:
		return "1_5_5_5";
	case GNM_IMG_DATA_FORMAT_5_5_5_1:
		return "5_5_5_1";
	case GNM_IMG_DATA_FORMAT_4_4_4_4:
		return "4_4_4_4";
	case GNM_IMG_DATA_FORMAT_8_24:
		return "8_24";
	case GNM_IMG_DATA_FORMAT_24_8:
		return "24_8";
	case GNM_IMG_DATA_FORMAT_X24_8_32:
		return "X24_8_32";
	case GNM_IMG_DATA_FORMAT_GB_GR:
		return "GB_GR";
	case GNM_IMG_DATA_FORMAT_BG_RG:
		return "BG_RG";
	case GNM_IMG_DATA_FORMAT_5_9_9_9:
		return "5_9_9_9";
	case GNM_IMG_DATA_FORMAT_BC1:
		return "BC1";
	case GNM_IMG_DATA_FORMAT_BC2:
		return "BC2";
	case GNM_IMG_DATA_FORMAT_BC3:
		return "BC3";
	case GNM_IMG_DATA_FORMAT_BC4:
		return "BC4";
	case GNM_IMG_DATA_FORMAT_BC5:
		return "BC5";
	case GNM_IMG_DATA_FORMAT_BC6:
		return "BC6";
	case GNM_IMG_DATA_FORMAT_BC7:
		return "BC7";
	case GNM_IMG_DATA_FORMAT_FMASK8_S2_F1:
		return "FMASK8_S2_F1";
	case GNM_IMG_DATA_FORMAT_FMASK8_S4_F1:
		return "FMASK8_S4_F1";
	case GNM_IMG_DATA_FORMAT_FMASK8_S8_F1:
		return "FMASK8_S8_F1";
	case GNM_IMG_DATA_FORMAT_FMASK8_S2_F2:
		return "FMASK8_S2_F2";
	case GNM_IMG_DATA_FORMAT_FMASK8_S4_F2:
		return "FMASK8_S4_F2";
	case GNM_IMG_DATA_FORMAT_FMASK8_S4_F4:
		return "FMASK8_S4_F4";
	case GNM_IMG_DATA_FORMAT_FMASK16_S16_F1:
		return "FMASK16_S16_F1";
	case GNM_IMG_DATA_FORMAT_FMASK16_S8_F2:
		return "FMASK16_S8_F2";
	case GNM_IMG_DATA_FORMAT_FMASK32_S16_F2:
		return "FMASK32_S16_F2";
	case GNM_IMG_DATA_FORMAT_FMASK32_S8_F4:
		return "FMASK32_S8_F4";
	case GNM_IMG_DATA_FORMAT_FMASK32_S8_F8:
		return "FMASK32_S8_F8";
	case GNM_IMG_DATA_FORMAT_FMASK64_S16_F4:
		return "FMASK64_S16_F4";
	case GNM_IMG_DATA_FORMAT_FMASK64_S16_F8:
		return "FMASK64_S16_F8";
	case GNM_IMG_DATA_FORMAT_4_4:
		return "4_4";
	case GNM_IMG_DATA_FORMAT_6_5_5:
		return "6_5_5";
	case GNM_IMG_DATA_FORMAT_1:
		return "1";
	case GNM_IMG_DATA_FORMAT_1_REVERSED:
		return "1_REVERSED";
	default:
		return "Unknown format";
	}
}

static inline const char* gnmStrTextureType(GnmTextureType type) {
	switch (type) {
	case GNM_TEXTURE_1D:
		return "1D";
	case GNM_TEXTURE_2D:
		return "2D";
	case GNM_TEXTURE_3D:
		return "3D";
	case GNM_TEXTURE_CUBEMAP:
		return "Cubemap";
	case GNM_TEXTURE_1D_ARRAY:
		return "1D Array";
	case GNM_TEXTURE_2D_ARRAY:
		return "2D_Array";
	case GNM_TEXTURE_2D_MSAA:
		return "2D_MSAA";
	case GNM_TEXTURE_2D_ARRAY_MSAA:
		return "2D Array MSAA";
	default:
		return "Unknown type";
	}
}

static inline const char* gnmStrTexChannelType(GnmImgNumFormat type) {
	switch (type) {
	case GNM_IMG_NUM_FORMAT_UNORM:
		return "UNORM";
	case GNM_IMG_NUM_FORMAT_SNORM:
		return "SNORM";
	case GNM_IMG_NUM_FORMAT_USCALED:
		return "USCALED";
	case GNM_IMG_NUM_FORMAT_SSCALED:
		return "SSCALED";
	case GNM_IMG_NUM_FORMAT_UINT:
		return "UINT";
	case GNM_IMG_NUM_FORMAT_SINT:
		return "SINT";
	case GNM_IMG_NUM_FORMAT_SNORM_OGL:
		return "SNORM_OGL";
	case GNM_IMG_NUM_FORMAT_FLOAT:
		return "FLOAT";
	case GNM_IMG_NUM_FORMAT_SRGB:
		return "SRGB";
	case GNM_IMG_NUM_FORMAT_UBNORM:
		return "UBNORM";
	case GNM_IMG_NUM_FORMAT_UBNORM_OGL:
		return "UBNORM_OGL";
	case GNM_IMG_NUM_FORMAT_UBINT:
		return "UBINT";
	case GNM_IMG_NUM_FORMAT_UBSCALED:
		return "UBSCALED";
	default:
		return "Unknown type";
	}
}

static inline const char* gnmStrTexChannel(GnmChannel chan) {
	switch (chan) {
	case GNM_CHAN_CONSTANT0:
		return "Constant 0";
	case GNM_CHAN_CONSTANT1:
		return "Constant 1";
	case GNM_CHAN_X:
		return "X";
	case GNM_CHAN_Y:
		return "Y";
	case GNM_CHAN_Z:
		return "Z";
	case GNM_CHAN_W:
		return "W";
	default:
		return "Unknown channel";
	}
}

static inline const char* gnmStrDccColorTransform(GnmDccColorTransform transform
) {
	switch (transform) {
	case GNM_DCC_CT_AUTO:
		return "Auto";
	case GNM_DCC_CT_NONE:
		return "None";
	case GNM_DCC_CT_ABGR:
		return "ABGR";
	case GNM_DCC_CT_BGRA:
		return "BGRA";
	default:
		return "Unknown transform";
	}
}

static inline const char* gnmStrArrayMode(GnmArrayMode mode) {
	switch (mode) {
	case GNM_ARRAY_LINEAR_GENERAL:
		return "LINEAR_GENERAL";
	case GNM_ARRAY_LINEAR_ALIGNED:
		return "LINEAR_ALIGNED";
	case GNM_ARRAY_1D_TILED_THIN1:
		return "1D_TILED_THIN1";
	case GNM_ARRAY_1D_TILED_THICK:
		return "1D_TILED_THICK";
	case GNM_ARRAY_2D_TILED_THIN1:
		return "2D_TILED_THIN1";
	case GNM_ARRAY_PRT_TILED_THIN1:
		return "PRT_TILED_THIN1";
	case GNM_ARRAY_PRT_2D_TILED_THIN1:
		return "PRT_2D_TILED_THIN1";
	case GNM_ARRAY_2D_TILED_THICK:
		return "2D_TILED_THICK";
	case GNM_ARRAY_2D_TILED_XTHICK:
		return "2D_TILED_X_THICK";
	case GNM_ARRAY_PRT_TILED_THICK:
		return "PRT_TILED_THICK";
	case GNM_ARRAY_PRT_2D_TILED_THICK:
		return "PRT_2D_TILED_THICK";
	case GNM_ARRAY_PRT_3D_TILED_THIN1:
		return "PRT_3D_TILED_THIN1";
	case GNM_ARRAY_3D_TILED_THIN1:
		return "3D_TILED_THIN1";
	case GNM_ARRAY_3D_TILED_THICK:
		return "3D_TILED_THICK";
	case GNM_ARRAY_3D_TILED_XTHICK:
		return "3D_TILED_XTHICK";
	case GNM_ARRAY_PRT_3D_TILED_THICK:
		return "PRT_3D_TILED_THICK";
	default:
		return "Unknown mode";
	}
}

static inline const char* gnmStrTileMode(GnmTileMode mode) {
	switch (mode) {
	case GNM_TM_DEPTH_2D_THIN_64:
		return "DEPTH_2D_THIN_64";
	case GNM_TM_DEPTH_2D_THIN_128:
		return "DEPTH_2D_THIN_128";
	case GNM_TM_DEPTH_2D_THIN_256:
		return "DEPTH_2D_THIN_256";
	case GNM_TM_DEPTH_2D_THIN_512:
		return "DEPTH_2D_THIN_512";
	case GNM_TM_DEPTH_2D_THIN_1K:
		return "DEPTH_2D_THIN_1K";
	case GNM_TM_DEPTH_1D_THIN:
		return "DEPTH_1D_THIN";
	case GNM_TM_DEPTH_2D_THIN_PRT_256:
		return "DEPTH_2D_THIN_PRT_256";
	case GNM_TM_DEPTH_2D_THIN_PRT_1K:
		return "DEPTH_2D_THIN_PRT_1K";
	case GNM_TM_DISPLAY_LINEAR_ALIGNED:
		return "DISPLAY_LINEAR_ALIGNED";
	case GNM_TM_DISPLAY_1D_THIN:
		return "DISPLAY_1D_THIN";
	case GNM_TM_DISPLAY_2D_THIN:
		return "DISPLAY_2D_THIN";
	case GNM_TM_DISPLAY_THIN_PRT:
		return "DISPLAY_THIN_PRT";
	case GNM_TM_DISPLAY_2D_THIN_PRT:
		return "DISPLAY_2D_THIN_PRT";
	case GNM_TM_THIN_1D_THIN:
		return "THIN_1D_THIN";
	case GNM_TM_THIN_2D_THIN:
		return "THIN_2D_THIN";
	case GNM_TM_THIN_3D_THIN:
		return "THIN_3D_THIN";
	case GNM_TM_THIN_THIN_PRT:
		return "THIN_THIN_PRT";
	case GNM_TM_THIN_2D_THIN_PRT:
		return "THIN_2D_THIN_PRT";
	case GNM_TM_THIN_3D_THIN_PRT:
		return "THIN_3D_THIN_PRT";
	case GNM_TM_THICK_1D_THICK:
		return "THICK_1D_THICK";
	case GNM_TM_THICK_2D_THICK:
		return "THICK_2D_THICK";
	case GNM_TM_THICK_3D_THICK:
		return "THICK_3D_THICK";
	case GNM_TM_THICK_THICK_PRT:
		return "THICK_THICK_PRT";
	case GNM_TM_THICK_2D_THICK_PRT:
		return "THICK_2D_THICK_PRT";
	case GNM_TM_THICK_3D_THICK_PRT:
		return "THICK_3D_THICK_PRT";
	case GNM_TM_THICK_2D_XTHICK:
		return "THICK_2D_XTHICK";
	case GNM_TM_THICK_3D_XTHICK:
		return "THICK_3D_XTHICK";
	case GNM_TM_DISPLAY_LINEAR_GENERAL:
		return "DISPLAY_LINEAR_GENERAL";
	default:
		return "Unknown mode";
	}
}

static inline const char* gnmStrMicroTileMode(GnmMicroTileMode mtm) {
	switch (mtm) {
	case GNM_SURF_DISPLAY_MICRO_TILING:
		return "DISPLAY";
	case GNM_SURF_THIN_MICRO_TILING:
		return "THIN";
	case GNM_SURF_DEPTH_MICRO_TILING:
		return "DEPTH";
	case GNM_SURF_ROTATED_MICRO_TILING:
		return "ROTATED";
	case GNM_SURF_THICK_MICRO_TILING:
		return "THICK";
	default:
		return "Unknown mode";
	}
}

static inline const char* gnmStrBankWidth(GnmBankWidth width) {
	switch (width) {
	case GNM_SURF_BANK_WIDTH_1:
		return "1";
	case GNM_SURF_BANK_WIDTH_2:
		return "2";
	case GNM_SURF_BANK_WIDTH_4:
		return "4";
	case GNM_SURF_BANK_WIDTH_8:
		return "8";
	default:
		return "Unknown width";
	}
}

static inline const char* gnmStrBankHeight(GnmBankHeight height) {
	switch (height) {
	case GNM_SURF_BANK_HEIGHT_1:
		return "1";
	case GNM_SURF_BANK_HEIGHT_2:
		return "2";
	case GNM_SURF_BANK_HEIGHT_4:
		return "4";
	case GNM_SURF_BANK_HEIGHT_8:
		return "8";
	default:
		return "Unknown height";
	}
}

static inline const char* gnmStrNumBanks(GnmNumBanks numbanks) {
	switch (numbanks) {
	case GNM_SURF_2_BANK:
		return "GNM_NUMBANKS_2";
	case GNM_SURF_4_BANK:
		return "GNM_NUMBANKS_4";
	case GNM_SURF_8_BANK:
		return "GNM_NUMBANKS_8";
	case GNM_SURF_16_BANK:
		return "GNM_NUMBANKS_16";
	default:
		return "Unknown number";
	}
}

static inline const char* gnmStrMacroTileAspect(const GnmMacroTileAspect aspect
) {
	switch (aspect) {
	case GNM_SURF_MACRO_ASPECT_1:
		return "1";
	case GNM_SURF_MACRO_ASPECT_2:
		return "2";
	case GNM_SURF_MACRO_ASPECT_4:
		return "4";
	case GNM_SURF_MACRO_ASPECT_8:
		return "8";
	default:
		return "Unknown aspect";
	}
}

static inline const char* gnmStrMacroTileMode(const GnmMacroTileMode mtm) {
	switch (mtm) {
	case GNM_MACROTILEMODE_1x4_16:
		return "1x4_16";
	case GNM_MACROTILEMODE_1x2_16:
		return "1x2_16";
	case GNM_MACROTILEMODE_1x1_16:
		return "1x1_16";
	case GNM_MACROTILEMODE_1x1_16_DUP:
		return "1x1_16_DUP";
	case GNM_MACROTILEMODE_1x1_8:
		return "1x1_8";
	case GNM_MACROTILEMODE_1x1_4:
		return "1x1_4";
	case GNM_MACROTILEMODE_1x1_2:
		return "1x1_2";
	case GNM_MACROTILEMODE_1x1_2_DUP:
		return "1x1_2_DUP";
	case GNM_MACROTILEMODE_1x8_16:
		return "1x8_16";
	case GNM_MACROTILEMODE_1x4_16_DUP:
		return "1x4_16_DUP";
	case GNM_MACROTILEMODE_1x2_16_DUP:
		return "1x2_16_DUP";
	case GNM_MACROTILEMODE_1x1_16_DUP2:
		return "1x1_16_DUP2";
	case GNM_MACROTILEMODE_1x1_8_DUP:
		return "1x1_8_DUP";
	case GNM_MACROTILEMODE_1x1_4_DUP:
		return "1x1_4_DUP";
	case GNM_MACROTILEMODE_1x1_2_DUP2:
		return "1x1_2_DUP2";
	case GNM_MACROTILEMODE_1x1_2_DUP3:
		return "1x1_2_DUP3";
	default:
		return "Unknown mode";
	}
}

static inline const char* gnmStrShaderInputUsageType(
    GnmShaderInputUsageType type
) {
	switch (type) {
	case GNM_SHINPUTUSAGE_IMM_RESOURCE:
		return "imm_resource";
	case GNM_SHINPUTUSAGE_IMM_SAMPLER:
		return "imm_sampler";
	case GNM_SHINPUTUSAGE_IMM_CONSTBUFFER:
		return "imm_constbuffer";
	case GNM_SHINPUTUSAGE_IMM_VERTEXBUFFER:
		return "imm_vertexbuffer";
	case GNM_SHINPUTUSAGE_IMM_RWRESOURCE:
		return "imm_rwresource";
	case GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST:
		return "imm_alufloatconst";
	case GNM_SHINPUTUSAGE_IMM_ALUBOOL32CONST:
		return "imm_alubool32const";
	case GNM_SHINPUTUSAGE_IMM_GDSCOUNTERRANGE:
		return "imm_gdscounterrange";
	case GNM_SHINPUTUSAGE_IMM_GDSMEMORYRANGE:
		return "imm_gdsmemoryrange";
	case GNM_SHINPUTUSAGE_IMM_GWSBASE:
		return "imm_gwsbase";
	case GNM_SHINPUTUSAGE_IMM_SRT:
		return "imm_srt";
	case GNM_SHINPUTUSAGE_IMM_LDSESGSSIZE:
		return "imm_ldsesgssize";
	case GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER:
		return "subptr_fetchshader";
	case GNM_SHINPUTUSAGE_PTR_RESOURCETABLE:
		return "ptr_resourcetable";
	case GNM_SHINPUTUSAGE_PTR_INTERNALRESOURCETABLE:
		return "ptr_internalresourcetable";
	case GNM_SHINPUTUSAGE_PTR_SAMPLERTABLE:
		return "ptr_samplertable";
	case GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE:
		return "ptr_constbuffertable";
	case GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE:
		return "ptr_vertexbuffertable";
	case GNM_SHINPUTUSAGE_PTR_SOBUFFERTABLE:
		return "ptr_sobuffertable";
	case GNM_SHINPUTUSAGE_PTR_RWRESOURCETABLE:
		return "ptr_rwresourcetable";
	case GNM_SHINPUTUSAGE_PTR_INTERNALGLOBALTABLE:
		return "ptr_internalglobaltable";
	case GNM_SHINPUTUSAGE_PTR_EXTENDEDUSERDATA:
		return "ptr_extendeduserdata";
	case GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE:
		return "ptr_indirectresourcetable";
	case GNM_SHINPUTUSAGE_PTR_INDIRECTINTERNALRESOURCETABLE:
		return "ptr_indirectinternalresourcetable";
	case GNM_SHINPUTUSAGE_PTR_INDIRECTRWRESOURCETABLE:
		return "ptr_indirectrwresourcetable";
	default:
		return "Unknown type";
	}
}

static inline const char* gnmStrPixelDefaultValue(GnmPixelDefaultValue type) {
	switch (type) {
	case GNM_PX_DEFVAL_NONE:
		return "None";
	case GNM_PX_DEFVAL_0_0_0_1:
		return "{0,0,0,1}";
	case GNM_PX_DEFVAL_1_1_1_0:
		return "{1,1,1,0}";
	case GNM_PX_DEFVAL_1_1_1_1:
		return "{1,1,1,1}";
	default:
		return "Unknown value";
	}
}

static inline const char* gnmStrShaderType(GnmShaderType type) {
	switch (type) {
	case GNM_SHADER_INVALID:
		return "Invalid shader";
	case GNM_SHADER_VERTEX:
		return "Vertex shader";
	case GNM_SHADER_PIXEL:
		return "Pixel shader";
	case GNM_SHADER_GEOMETRY:
		return "Geometry shader";
	case GNM_SHADER_COMPUTE:
		return "Compute shader";
	case GNM_SHADER_EXPORT:
		return "Export shader";
	case GNM_SHADER_LOCAL:
		return "Local shader";
	case GNM_SHADER_HULL:
		return "Hull shader";
	default:
		return "Unknown shader";
	}
}

static inline const char* gnmStrShaderBinaryType(GnmShaderBinaryType type) {
	switch (type) {
	case GNM_SHB_PS:
		return "Pixel Shader";
	case GNM_SHB_VS_VS:
		return "Vertex Shader (VS)";
	case GNM_SHB_VS_ES:
		return "Vertex Shader (ES)";
	case GNM_SHB_VS_LS:
		return "Vertex Shader (LS)";
	case GNM_SHB_CS:
		return "Compute Shader";
	case GNM_SHB_GS:
		return "Geometry Shader";
	case GNM_SHB_GS_VS:
		return "Geometry Shader (VS)";
	case GNM_SHB_HS:
		return "Hull Shader";
	case GNM_SHB_DS_VS:
		return "Domain Shader (VS)";
	case GNM_SHB_DS_ES:
		return "Domain Shader (ES)";
	default:
		return "Unknown shader";
	}
}

static inline const char* gnmStrTargetGpuMode(GnmTargetGpuMode mode) {
	if (mode & GNM_TARGETGPUMODE_BASE && mode & GNM_TARGETGPUMODE_NEO) {
		return "Base and NEO modes";
	}
	switch (mode) {
	case GNM_TARGETGPUMODE_UNSPECIFIED:
		return "Unspecified";
	case GNM_TARGETGPUMODE_BASE:
		return "Base mode";
	case GNM_TARGETGPUMODE_NEO:
		return "NEO mode";
	default:
		return "Unknown mode";
	}
}

static inline const char* gnmStrShaderStage(GnmShaderStage stage) {
	switch (stage) {
	case GNM_STAGE_CS:
		return "Compute";
	case GNM_STAGE_PS:
		return "Pixel";
	case GNM_STAGE_VS:
		return "Vertex";
	case GNM_STAGE_GS:
		return "Geometry";
	case GNM_STAGE_ES:
		return "Export";
	case GNM_STAGE_HS:
		return "Hull";
	case GNM_STAGE_LS:
		return "Local";
	default:
		return "Unknown";
	}
}

#endif	// _GNM_STRINGS_H_
