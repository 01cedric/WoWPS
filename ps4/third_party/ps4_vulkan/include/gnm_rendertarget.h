#ifndef _GNM_RENDERTARGET_H_
#define _GNM_RENDERTARGET_H_

#include <stdint.h>

#include "gnm_dataformat.h"
#include "gnm_error.h"
#include "gnm_types.h"
#include "pm4/gpuaddr_types.h"

OPENGNM_EXTERN_C_BEGIN

typedef struct {
	uint32_t enable_cmask_fastclear : 1;
	uint32_t enable_fmask_compression : 1;
	uint32_t enable_colortexture_without_decompress : 1;
	uint32_t enable_fmasktexture_without_decompress : 1;
	uint32_t enable_dcc_compression : 1;
	uint32_t _unused : 27;
} GnmRenderTargetCreateInfoFlags;

typedef struct {
	GnmDataFormat colorfmt;

	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t numslices;

	uint32_t numsamples;
	uint32_t numfragments;

	GnmTileMode colortilemodehint;
	GnmGpuMode mingpumode;
	GnmRenderTargetCreateInfoFlags flags;
} GnmRenderTargetCreateInfo;

typedef struct {
	uint32_t base_base256b;

	union {
		struct {
			uint32_t tilemax : 11;
			uint32_t _unused : 9;
			uint32_t fmask_tilemax : 11;
			uint32_t _unused2 : 1;
		};
		uint32_t asuint;
	} pitch;

	union {
		struct {
			uint32_t tilemax : 22;
			uint32_t _unused : 10;
		};
		uint32_t asuint;
	} slice;

	union {
		struct {
			uint32_t slicestart : 11;
			uint32_t _unused : 2;
			uint32_t slicemax : 11;
			uint32_t _unused2 : 8;
		};
		uint32_t asuint;
	} view;

	union {
		struct {
			uint32_t _unused : 2;
			uint32_t format : 5;
			uint32_t _unused2 : 1;
			GnmSurfaceNumber channeltype : 3;
			GnmSurfaceSwap channelorder : 2;
			uint32_t fast_clear : 1;
			uint32_t compression : 1;
			uint32_t is_normalized : 1;
			uint32_t is_int : 1;
			uint32_t simple_float : 1;
			uint32_t is_scaled : 1;
			uint32_t cmask_is_linear : 1;
			uint32_t _unused4 : 6;
			uint32_t fmask_compression_mode : 2;
			uint32_t dcc_enable : 1;
			uint32_t cmask_addr_type : 2;
			uint32_t alt_tile_mode : 1;
		};
		uint32_t asuint;
	} info;

	union {
		struct {
			GnmTileMode tilemode_index : 5;
			GnmTileMode fmask_tilemode_index : 5;
			uint32_t _unused : 2;
			uint32_t num_samples : 3;
			uint32_t num_fragments : 2;
			uint32_t force_dst_alpha_1 : 1;
			uint32_t _unused2 : 14;
		};
		uint32_t asuint;
	} attrib;

	union {
		struct {
			uint32_t overwrite_combine_disabler : 1;
			uint32_t _unused : 1;
			uint32_t max_uncompressed_blocksize : 2;
			uint32_t min_compressed_blocksize : 1;
			uint32_t max_compressed_blocksize : 2;
			uint32_t color_transform : 2;
			uint32_t independent_64b_blocks : 1;
			uint32_t _unused5 : 22;
		};
		uint32_t asuint;
	} dcc_control;

	uint32_t cmask_base256b;
	union {
		struct {
			uint32_t tilemax : 14;
			uint32_t _unused : 18;
		};
		uint32_t asuint;
	} cmask_slice;

	uint32_t fmask_base256b;
	union {
		struct {
			uint32_t tilemax : 22;
			uint32_t _unused : 10;
		};
		uint32_t asuint;
	} fmask_slice;

	uint32_t clear_word0;
	uint32_t clear_word1;

	uint32_t dccbase_base256b;

	uint32_t _unusedreg;

	union {
		struct {
			uint16_t width;
			uint16_t height;
		};
		uint32_t asuint;
	} size;
} GnmRenderTarget;
_Static_assert(sizeof(GnmRenderTarget) == 0x40, "");

GnmError sceGnmCreateRenderTarget(
    GnmRenderTarget* rt, const GnmRenderTargetCreateInfo* createinfo
);

GnmDataFormat sceGnmRtGetFormat(const GnmRenderTarget* rt);

static inline void* sceGnmRtGetBaseAddr(const GnmRenderTarget* rt) {
	return (void*)((uintptr_t)rt->base_base256b << 8);
}
static inline void sceGnmRtSetBaseAddr(GnmRenderTarget* rt, void* baseaddr) {
	rt->base_base256b = ((uintptr_t)baseaddr >> 8);
}

static inline uint32_t sceGnmRtGetPitch(const GnmRenderTarget* rt) {
	return (rt->pitch.tilemax + 1) * 8;
}
static inline uint32_t sceGnmRtGetSliceSize(const GnmRenderTarget* rt) {
	return (rt->slice.tilemax + 1) * 64 / sceGnmRtGetPitch(rt);
}
static inline uint32_t sceGnmRtGetNumSlices(const GnmRenderTarget* rt) {
	return rt->view.slicemax + 1;
}

static inline uint8_t sceGnmRtGetNumSamples(const GnmRenderTarget* rt) {
	return 1 << rt->attrib.num_samples;
}
static inline uint8_t sceGnmRtGetNumFragments(const GnmRenderTarget* rt) {
	return 1 << rt->attrib.num_fragments;
}

static inline GpaTextureInfo sceGnmRtBuildInfo(const GnmRenderTarget* rt) {
	GpaTextureInfo info;
	info.type = GNM_TEXTURE_2D;
	info.fmt = sceGnmRtGetFormat(rt);
	info.width = sceGnmRtGetPitch(rt);
	info.height = sceGnmRtGetSliceSize(rt);
	info.pitch = sceGnmRtGetPitch(rt);
	info.depth = 1;
	info.numfrags = sceGnmRtGetNumFragments(rt);
	info.nummips = 1;
	info.numslices = 1;
	info.tm = (GnmTileMode)rt->attrib.tilemode_index;
	info.mingpumode = rt->info.alt_tile_mode ? GNM_GPU_NEO : GNM_GPU_BASE;
	info.pow2pad = false;
	return info;
}

GnmError sceGnmRtCalcByteSize(
    uint64_t* outsize, uint32_t* outalign, const GnmRenderTarget* rt
);

OPENGNM_EXTERN_C_END

#endif /* _GNM_RENDERTARGET_H_ */
