#ifndef _GNM_DEPTHRENDERTARGET_H_
#define _GNM_DEPTHRENDERTARGET_H_

#include <stdint.h>

#include "gnm_dataformat.h"
#include "gnm_error.h"
#include "gnm_types.h"

OPENGNM_EXTERN_C_BEGIN

typedef struct {
	uint32_t enable_htile_acceleration : 1;
	uint32_t enable_texture_without_decompress : 1;
	uint32_t _unused : 30;
} GnmDepthRenderTargetCreateInfoFlags;

typedef struct {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t numslices;

	GnmZFormat zfmt;
	GnmStencilFormat stencilfmt;
	GnmTileMode tilemodehint;
	GnmGpuMode mingpumode;
	uint32_t numfragments;
	GnmDepthRenderTargetCreateInfoFlags flags;
} GnmDepthRenderTargetCreateInfo;

typedef struct {
	union {
		struct {
			GnmZFormat format : 2;
			uint32_t numsamples : 2;
			uint32_t _unused : 9;
			uint32_t tilesplit : 3;
			uint32_t _unused2 : 4;
			uint32_t tilemodeindex : 3;
			uint32_t _unused3 : 4;
			uint32_t allowexpclear : 1;
			uint32_t _unused4 : 1;
			uint32_t tilesurfaceenable : 1;
			uint32_t _unused5 : 1;
			uint32_t zrangeprecision : 1;
		};
		uint32_t asuint;
	} zinfo;

	union {
		struct {
			GnmStencilFormat format : 1;
			uint32_t _unused : 12;
			uint32_t tilesplit : 3;
			uint32_t _unused2 : 4;
			uint32_t tilemodeindex : 3;
			uint32_t _unused3 : 4;
			uint32_t allowexpclear : 1;
			uint32_t _unused4 : 1;
			uint32_t tilestencildisable : 1;
			uint32_t _unused5 : 2;
		};
		uint32_t asuint;
	} stencilinfo;

	uint32_t zreadbase256b;
	uint32_t stencilreadbase256b;
	uint32_t zwritebase256b;
	uint32_t stencilwritebase256b;

	union {
		struct {
			uint32_t pitchtilemax : 11;
			uint32_t heighttilemax : 11;
			uint32_t _unused : 10;
		};
		uint32_t asuint;
	} depthsize;

	union {
		struct {
			uint32_t slicetile : 22;
			uint32_t _unused : 10;
		};
		uint32_t asuint;
	} depthslice;

	union {
		struct {
			uint32_t slicestart : 11;
			uint32_t _unused : 2;
			uint32_t slicemax : 11;
			uint32_t _unused2 : 8;
		};
		uint32_t asuint;
	} depthview;

	uint32_t htiledatabase256b;

	union {
		struct {
			uint32_t linear : 1;
			uint32_t _unused : 16;
			uint32_t tccompatible : 1;
			uint32_t _unused2 : 14;
		};
		uint32_t asuint;
	} htilesurface;

	union {
		struct {
			uint32_t _unused : 4;
			GnmArrayMode arraymode : 4;
			GnmPipeConfig pipeconfig : 5;
			GnmBankWidth bankwidth : 2;
			GnmBankHeight bankheight : 2;
			GnmMacroTileAspect macrotileaspect : 2;
			GnmNumBanks numbanks : 2;
			uint32_t _unused2 : 11;
		};
		uint32_t asuint;
	} depthinfo;

	union {
		struct {
			uint16_t width;
			uint16_t height;
		};
		uint32_t asuint;
	} size;
} GnmDepthRenderTarget;
_Static_assert(sizeof(GnmDepthRenderTarget) == 0x34, "");

GnmError sceGnmCreateDepthRenderTarget(
    GnmDepthRenderTarget* drt, const GnmDepthRenderTargetCreateInfo* createinfo
);

GnmError sceGnmDrtCalcByteSize(
    uint64_t* outsize, uint32_t* outalignment, const GnmDepthRenderTarget* drt
);
GnmError sceGnmDrtCalcStencilByteOffset(
    uint64_t* outoffset, const GnmDepthRenderTarget* drt
);

static inline uint8_t sceGnmDrtGetNumFragments(const GnmDepthRenderTarget* drt) {
	return 1 << drt->zinfo.numsamples;
}
static inline void sceGnmDrtSetNumFragments(
    GnmDepthRenderTarget* drt, uint32_t numfrags
) {
	drt->zinfo.numsamples = numfrags >> 1;
}
GnmError sceGnmDrtSetTileMode(GnmDepthRenderTarget* drt, GnmTileMode mode);

void* sceGnmDrtGetZReadAddress(const GnmDepthRenderTarget* drt);
GnmError sceGnmDrtSetZReadAddress(GnmDepthRenderTarget* drt, void* baseaddr);

void* sceGnmDrtGetStencilReadAddress(const GnmDepthRenderTarget* drt);
GnmError sceGnmDrtSetStencilReadAddress(GnmDepthRenderTarget* drt, void* baseaddr);

void* sceGnmDrtGetZWriteAddress(const GnmDepthRenderTarget* drt);
GnmError sceGnmDrtSetZWriteAddress(GnmDepthRenderTarget* drt, void* baseaddr);

void* sceGnmDrtGetStencilWriteAddress(const GnmDepthRenderTarget* drt);
GnmError sceGnmDrtSetStencilWriteAddress(
    GnmDepthRenderTarget* drt, void* baseaddr
);

static inline uint16_t sceGnmDrtGetSliceSize(const GnmDepthRenderTarget* drt) {
	return (drt->depthslice.slicetile + 1) * 64;
}
static inline void sceGnmDrtSetSliceSize(
    GnmDepthRenderTarget* drt, uint16_t pitch, uint16_t height
) {
	const uint16_t size = (height * pitch / 64) - 1;
	drt->depthslice.slicetile = size;
}

static inline uint16_t sceGnmDrtGetPaddedWidth(const GnmDepthRenderTarget* drt) {
	return (drt->depthsize.pitchtilemax + 1) * 8;
}
static inline GnmError sceGnmDrtSetPaddedWidth(
    GnmDepthRenderTarget* drt, uint16_t width
) {
	const uint32_t pitch = (width / 8) - 1;
	if (pitch > 2047) {
		return GNM_ERROR_INVALID_ARGS;
	}
	drt->depthsize.pitchtilemax = pitch;
	return GNM_ERROR_OK;
}
static inline uint16_t sceGnmDrtGetPaddedHeight(const GnmDepthRenderTarget* drt) {
	return (drt->depthsize.heighttilemax + 1) * 8;
}
static inline GnmError sceGnmDrtSetPaddedHeight(
    GnmDepthRenderTarget* drt, uint16_t height
) {
	const uint32_t pheight = (height / 8) - 1;
	if (pheight > 2047) {
		return GNM_ERROR_INVALID_ARGS;
	}
	drt->depthsize.heighttilemax = pheight;
	return GNM_ERROR_OK;
}

static inline uint16_t sceGnmDrtGetNumSlices(const GnmDepthRenderTarget* drt) {
	return drt->depthview.slicemax + 1;
}

static inline void* sceGnmDrtGetHtileAddress(const GnmDepthRenderTarget* drt) {
	return (void*)((uintptr_t)drt->htiledatabase256b << 8);
}
static inline GnmError sceGnmDrtSetHtileAddress(
    GnmDepthRenderTarget* drt, void* baseaddr
) {
	if (!drt) {
		return GNM_ERROR_INVALID_ARGS;
	}
	if ((uintptr_t)baseaddr & 0xff) {
		return GNM_ERROR_INVALID_ALIGNMENT;
	}
	drt->htiledatabase256b = (uintptr_t)baseaddr >> 8;
	return GNM_ERROR_OK;
}

static inline GnmGpuMode sceGnmDrtGetMinGpuMode(const GnmDepthRenderTarget* drt) {
	return drt->depthinfo.pipeconfig == GNM_ADDR_SURF_P16_32x32_8x16
		   ? GNM_GPU_NEO
		   : GNM_GPU_BASE;
}

static inline uint16_t sceGnmDrtGetWidth(const GnmDepthRenderTarget* drt) {
	return drt->size.width;
}
static inline void sceGnmDrtSetWidth(GnmDepthRenderTarget* drt, uint16_t width) {
	drt->size.width = width;
}
static inline uint16_t sceGnmDrtGetHeight(const GnmDepthRenderTarget* drt) {
	return drt->size.height;
}
static inline void sceGnmDrtSetHeight(GnmDepthRenderTarget* drt, uint16_t height) {
	drt->size.height = height;
}

OPENGNM_EXTERN_C_END

#endif /* _GNM_DEPTHRENDERTARGET_H_ */
