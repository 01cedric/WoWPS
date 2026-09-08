#ifndef _GNM_SHADERBINARY_H_
#define _GNM_SHADERBINARY_H_

#include "gnm_shader.h"

#define GNM_SHADER_FILE_HEADER_ID 0x72646853  // Shdr
#define GNM_SHADER_BINARY_INFO_MAGIC "OrbShdr"

typedef enum {
	GNM_SHADER_INVALID = 0x0,
	GNM_SHADER_VERTEX = 0x1,
	GNM_SHADER_PIXEL = 0x2,
	GNM_SHADER_GEOMETRY = 0x3,
	GNM_SHADER_COMPUTE = 0x4,
	GNM_SHADER_EXPORT = 0x5,
	GNM_SHADER_LOCAL = 0x6,
	GNM_SHADER_HULL = 0x7,
} GnmShaderType;

typedef enum {
	GNM_TARGETGPUMODE_UNSPECIFIED = 0x0,
	GNM_TARGETGPUMODE_BASE = 0x1,
	GNM_TARGETGPUMODE_NEO = 0x2,
} GnmTargetGpuMode;

typedef struct {
	uint32_t magic;	 // GNM_SHADER_FILE_HEADER_ID
	uint16_t vermajor;
	uint16_t verminor;
	uint8_t type;  // GnmShaderType
	uint8_t headersizedwords;
	uint8_t auxdata;
	uint8_t targetgpumodes;	 // GnmTargetGpuMode
	uint32_t _unused;
} GnmShaderFileHeader;
_Static_assert(sizeof(GnmShaderFileHeader) == 0x10, "");

typedef struct {
	uint32_t shadersize : 23;
	uint32_t isusingsrt : 1;
	uint32_t numinputusageslots : 8;
	uint16_t embeddedconstantbufferdqwords;	 // 16 byte double q words
	uint16_t scratchsizeperthreaddwords;
} GnmShaderCommonData;
_Static_assert(sizeof(GnmShaderCommonData) == 0x8, "");

static inline uint32_t sceGnmShaderCommonCodeSize(const GnmShaderCommonData* data
) {
	return data->shadersize + data->embeddedconstantbufferdqwords * 16;
}

typedef enum {
	GNM_VSGSMODE_G = 0x1,

	GNM_VSGSMODE_G_ONCHIP = 0x2,
	GNM_VSGSMODE_G_ES_PASSTHRU = 0x4,
	GNM_VSGSMODE_G_ES_ELEMENTINFO = 0x8,
	GNM_VSGSMODE_G_CUTMODE_1024 = 0x0,
	GNM_VSGSMODE_G_CUTMODE_512 = 0x10,
	GNM_VSGSMODE_G_CUTMODE_256 = 0x20,
	GNM_VSGSMODE_G_CUTMODE_128 = 0x30,
	GNM_VSGSMODE_G_SUPRESSCUTS = 0x40,
	GNM_VSGSMODE_G_CUTMODE_MASK = 0x30,
	GNM_VSGSMODE_G_CUTMODE_SHIFT = 0x4,

	GNM_VSGSMODE_OFF = 0x0,
	GNM_VSGSMODE_A = 0x2,
	GNM_VSGSMODE_B = 0x4,
	GNM_VSGSMODE_C = 0x8,
	GNM_VSGSMODE_SPRITE_EN = 0xA,
	GNM_VSGSMODE_MASK = 0xE,
	GNM_VSGSMODE_CPACK = 0x10,
} GnmVsShaderGsMode;

typedef struct {
	GnmShaderCommonData common;
	GnmVsStageRegisters registers;

	uint8_t numinputsemantics;
	uint8_t numexportsemantics;
	uint8_t gsmodeornuminputsemanticscs;  // GnmVsShaderGsMode
	uint8_t fetchcontrol;
} GnmVsShader;
_Static_assert(sizeof(GnmVsShader) == 0x28, "");

typedef struct {
	GnmShaderCommonData common;
	GnmPsStageRegisters registers;

	uint8_t numinputsemantics;
	uint8_t _unused[3];
} GnmPsShader;
_Static_assert(sizeof(GnmPsShader) == 0x3c, "");

typedef struct {
	GnmShaderCommonData common;
	GnmCsStageRegisters registers;
} GnmCsShader;
_Static_assert(sizeof(GnmCsShader) == 0x24, "");

typedef struct {
	GnmShaderCommonData common;
	GnmGsStageRegisters registers;

	uint8_t numinputsemantics;
	uint8_t numexportsemantics;
	uint8_t _unused[2];
} GnmGsShader;
_Static_assert(sizeof(GnmGsShader) == 0x28, "");

typedef struct {
	GnmShaderCommonData common;
	GnmHsStageRegisters registers;

	uint8_t numinputsemantics;
	uint8_t _unused[3];
} GnmHsShader;
_Static_assert(sizeof(GnmHsShader) == 0x28, "");

/* Domain shader (TES) uses GnmVsShader since it outputs vertices.
 * The GnmShaderBinaryType field distinguishes DS_VS from VS_VS. */

typedef struct {
	GnmShaderCommonData common;
	GnmEsStageRegisters registers;

	uint8_t numinputsemantics;
	uint8_t numexportsemantics;
	uint8_t _unused[2];
} GnmEsShader;
_Static_assert(sizeof(GnmEsShader) == 0x1c, "");

typedef struct {
	GnmShaderCommonData common;
	GnmLsStageRegisters registers;

	uint8_t numinputsemantics;
	uint8_t numexportsemantics;
	uint8_t _unused[2];
} GnmLsShader;
_Static_assert(sizeof(GnmLsShader) == 0x1c, "");

typedef enum {
	GNM_SHB_PS = 0,
	GNM_SHB_VS_VS = 1,
	GNM_SHB_VS_ES = 2,
	GNM_SHB_VS_LS = 3,
	GNM_SHB_CS = 4,
	GNM_SHB_GS = 5,
	GNM_SHB_GS_VS = 6,
	GNM_SHB_HS = 7,
	GNM_SHB_DS_VS = 8,
	GNM_SHB_DS_ES = 9,
} GnmShaderBinaryType;

typedef struct {
	uint8_t signature[7];  // GNM_SHADER_BINARY_INFO_MAGIC
	uint8_t version;

	// if true, it's PSSL/CG. else it's IL/shtb
	uint32_t ispsslcg : 1;
	// is debugging source cached?
	uint32_t issourcecached : 1;
	uint32_t type : 4;	  // GnmShaderBinaryType
	uint32_t sourcetype : 2;  // ShaderSourceType
	// Shader code length
	uint32_t length : 24;

	// starts at ((uint32_t*)&ShaderBinaryInfo) - chunkusagebaseoffsetdwords
	uint8_t chunkusagebaseoffsetdwords;
	uint8_t numinputusageslots;
	uint8_t hassrt : 1;
	uint8_t hassrtusedvalidinfo : 1;
	uint8_t hasextendedusageinfo : 1;
	uint8_t _unused : 5;
	uint8_t _unused2;

	uint32_t shaderhash0;
	uint32_t shaderhash1;
	// CRC32 of whole shader until this field
	uint32_t crc32;
} GnmShaderBinaryInfo;
_Static_assert(sizeof(GnmShaderBinaryInfo) == 0x1c, "");

static inline int32_t sceGnmShaderInputUsageTypeSize(GnmShaderInputUsageType type
) {
	switch (type) {
	case GNM_SHINPUTUSAGE_IMM_RESOURCE:
	case GNM_SHINPUTUSAGE_IMM_RWRESOURCE:
	case GNM_SHINPUTUSAGE_IMM_SRT:
		return 0;
	case GNM_SHINPUTUSAGE_IMM_SAMPLER:
	case GNM_SHINPUTUSAGE_IMM_CONSTBUFFER:
	case GNM_SHINPUTUSAGE_IMM_VERTEXBUFFER:
		return 4;
	case GNM_SHINPUTUSAGE_IMM_ALUFLOATCONST:
	case GNM_SHINPUTUSAGE_IMM_ALUBOOL32CONST:
	case GNM_SHINPUTUSAGE_IMM_GDSCOUNTERRANGE:
	case GNM_SHINPUTUSAGE_IMM_GDSMEMORYRANGE:
	case GNM_SHINPUTUSAGE_IMM_GWSBASE:
	case GNM_SHINPUTUSAGE_IMM_LDSESGSSIZE:
		return 1;
	case GNM_SHINPUTUSAGE_SUBPTR_FETCHSHADER:
	case GNM_SHINPUTUSAGE_PTR_RESOURCETABLE:
	case GNM_SHINPUTUSAGE_PTR_INTERNALRESOURCETABLE:
	case GNM_SHINPUTUSAGE_PTR_SAMPLERTABLE:
	case GNM_SHINPUTUSAGE_PTR_CONSTBUFFERTABLE:
	case GNM_SHINPUTUSAGE_PTR_VERTEXBUFFERTABLE:
	case GNM_SHINPUTUSAGE_PTR_SOBUFFERTABLE:
	case GNM_SHINPUTUSAGE_PTR_RWRESOURCETABLE:
	case GNM_SHINPUTUSAGE_PTR_INTERNALGLOBALTABLE:
	case GNM_SHINPUTUSAGE_PTR_EXTENDEDUSERDATA:
	case GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE:
	case GNM_SHINPUTUSAGE_PTR_INDIRECTINTERNALRESOURCETABLE:
	case GNM_SHINPUTUSAGE_PTR_INDIRECTRWRESOURCETABLE:
		return 2;
	default:
		return -1;
	}
}

static inline const GnmShaderCommonData* sceGnmShfCommonData(
    const GnmShaderFileHeader* shf
) {
	if (!shf) {
		return 0;
	}
	const uint8_t* ptr = (const uint8_t*)shf + sizeof(GnmShaderFileHeader);
	return (const GnmShaderCommonData*)ptr;
}

static inline const GnmInputUsageSlot* sceGnmVsShaderInputUsageSlotTable(
    const GnmVsShader* vs
) {
	return (const GnmInputUsageSlot*)((const uint8_t*)vs +
					  sizeof(GnmVsShader));
}
static inline const GnmVertexInputSemantic* sceGnmVsShaderInputSemanticTable(
    const GnmVsShader* vs
) {
	const uint8_t* ptr =
	    (const uint8_t*)vs + sizeof(GnmVsShader) +
	    vs->common.numinputusageslots * sizeof(GnmInputUsageSlot);
	return (const GnmVertexInputSemantic*)ptr;
}
static inline const GnmVertexExportSemantic* sceGnmVsShaderExportSemanticTable(
    const GnmVsShader* vs
) {
	const uint8_t* ptr =
	    (const uint8_t*)vs + sizeof(GnmVsShader) +
	    vs->common.numinputusageslots * sizeof(GnmInputUsageSlot) +
	    vs->numinputsemantics * sizeof(GnmVertexInputSemantic);
	return (const GnmVertexExportSemantic*)ptr;
}
static inline uint32_t sceGnmVsShaderCalcSize(const GnmVsShader* vs) {
	const uint32_t size =
	    sizeof(GnmVsShader) +
	    sizeof(GnmInputUsageSlot) * vs->common.numinputusageslots +
	    sizeof(GnmVertexInputSemantic) * vs->numinputsemantics +
	    sizeof(GnmVertexExportSemantic) * vs->numexportsemantics;
	return (size + 3) & ~3;
}
static inline const void* sceGnmVsShaderCodePtr(const GnmVsShader* vs) {
	if (!vs) {
		return 0;
	}
	const uint32_t offset = vs->registers.spishaderpgmlovs;
	return (const uint8_t*)vs + offset;
}

static inline const GnmInputUsageSlot* sceGnmPsShaderInputUsageSlotTable(
    const GnmPsShader* ps
) {
	return (const GnmInputUsageSlot*)((const uint8_t*)ps +
					  sizeof(GnmPsShader));
}
static inline const GnmPixelInputSemantic* sceGnmPsShaderInputSemanticTable(
    const GnmPsShader* ps
) {
	const uint8_t* ptr =
	    (const uint8_t*)ps + sizeof(GnmPsShader) +
	    ps->common.numinputusageslots * sizeof(GnmInputUsageSlot);
	return (const GnmPixelInputSemantic*)ptr;
}
static inline uint32_t sceGnmPsShaderCalcSize(const GnmPsShader* ps) {
	const uint32_t size =
	    sizeof(GnmPsShader) +
	    sizeof(GnmInputUsageSlot) * ps->common.numinputusageslots +
	    sizeof(GnmPixelInputSemantic) * ps->numinputsemantics;
	return (size + 3) & ~3;
}
static inline const void* sceGnmPsShaderCodePtr(const GnmPsShader* ps) {
	if (!ps) {
		return 0;
	}
	const uint32_t offset = ps->registers.spishaderpgmlops;
	return (const uint8_t*)ps + offset;
}

#endif	// _GNM_SHADERBINARY_H_
