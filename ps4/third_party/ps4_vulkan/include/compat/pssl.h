#ifndef OPENGNM_COMPAT_PSSL_H
#define OPENGNM_COMPAT_PSSL_H

/*
 * Minimal PSSL binary format types for downstream consumers that
 * need to parse shader binary headers (e.g. freegnm-examples shared code).
 * Only the types actually used by downstream code are included here.
 * Full PSSL validation/string tools remain in the separate opengnm-tools repo.
 */

#include <stdint.h>
#include <gnm_shaderbinary.h>

typedef enum {
	PSSL_SHADER_VS = 0x0,
	PSSL_SHADER_FS = 0x1,
	PSSL_SHADER_CS = 0x2,
	PSSL_SHADER_GS = 0x3,
	PSSL_SHADER_HS = 0x4,
	PSSL_SHADER_DS = 0x5,
} PsslShaderType;

typedef enum {
	PSSL_CODE_IL = 0x0,
	PSSL_CODE_ISA = 0x1,
	PSSL_CODE_SCU = 0x2,
} PsslCodeType;

typedef enum {
	PSSL_COMPILER_UNSPECIFIED = 0,
	PSSL_COMPILER_ORBIS_PSSLC = 1,
	PSSL_COMPILER_ORBIS_ESSLC = 2,
	PSSL_COMPILER_ORBIS_WAVE = 3,
	PSSL_COMPILER_ORBIS_CU_AS = 4,
} PsslCompilerType;

typedef struct {
	uint8_t vertexvariant;
	uint8_t domainvariant;
	uint8_t geometryvariant;
	uint8_t hullvariant;
} PsslPipelineStage;
_Static_assert(sizeof(PsslPipelineStage) == 0x4, "");

typedef union {
	struct {
		uint16_t numthreads[3];
	} cs;
	struct {
		uint16_t instance;
		uint16_t maxvertexcount;
		uint8_t inputtype;
		uint8_t outputtype;
		uint8_t patchsize;
	} gs;
	struct {
		uint8_t patchtype;
		uint8_t inputcontrolpoints;
	} ds;
	struct {
		uint8_t patchtype;
		uint8_t inputcontrolpoints;
		uint8_t outputtopologytype;
		uint8_t partitioningtype;
		uint8_t outputcontrolpoints;
		uint8_t patchsize;
		uint8_t _unused[2];
		float maxtessfactor;
	} hs;
} PsslSystemAttributes;
_Static_assert(sizeof(PsslSystemAttributes) == 0xC, "");

typedef struct {
	uint8_t vermajor;
	uint8_t verminor;
	uint16_t compiler_revision;

	uint32_t association_hash0;
	uint32_t association_hash1;

	uint8_t shadertype;
	uint8_t codetype;
	uint8_t uses_srt;
	uint8_t compilertype;

	uint32_t codesize;

	PsslPipelineStage stageinfo;
	PsslSystemAttributes sysattributeinfo;
} PsslBinaryHeader;
_Static_assert(sizeof(PsslBinaryHeader) == 0x24, "");

#endif /* OPENGNM_COMPAT_PSSL_H */
