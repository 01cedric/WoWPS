#ifndef _GPUADDR_H_
#define _GPUADDR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gnm_dataformat.h"
#include "gnm_types.h"
#include "pm4/gpuaddr_types.h"

OPENGNM_EXTERN_C_BEGIN

/* === GpuAddr error codes === */
typedef enum {
	GPA_ERR_OK = 0,
	GPA_ERR_INVALID_ARGS,
	GPA_ERR_OVERFLOW,
	GPA_ERR_TILING_ERROR,
	GPA_ERR_UNSUPPORTED,
	GPA_ERR_INTERNAL_ERROR,
	GPA_ERR_NOT_COMPRESSED,
} GpaError;

const char* sceGpaStrError(const GpaError err);

/* === Surface computation === */
GpaError sceGpaComputeSurfaceInfo(GpaSurfaceInfo* out, const GpaTilingParams* tp);
GpaError sceGpaComputeHtileInfo(
    GpaHtileInfo* outinfo, const GpaHtileParams* params
);
GpaError sceGpaComputeCmaskInfo(
    GpaCmaskInfo* outinfo, const GpaCmaskParams* params
);
GpaError sceGpaComputeFmaskInfo(
    GpaFmaskInfo* outinfo, const GpaFmaskParams* params
);
GpaError sceGpaComputeSurfaceTileMode(
    GnmTileMode* outtilemode, GnmGpuMode mingpumode, GnmArrayMode arraymode,
    GpaSurfaceFlags flags, GnmDataFormat surfacefmt, uint32_t numfragsperpixel,
    GnmMicroTileMode mtm
);

GpaError sceGpaInitSurfaceContext(
    GpaSurfaceContext* ctx, size_t surfsize, const GpaTilingParams* tp
);
GpaError sceGpaComputeSurfaceCoord(
    uint64_t* outoffset, uint64_t* outbitoffset, const GpaSurfaceContext* ctx,
    uint32_t x, uint32_t y, uint32_t z, uint32_t fragindex
);
GpaError sceGpaComputeSurfaceSizeOffset(
    uint64_t* outsize, uint64_t* outoffset, const GpaTextureInfo* tex,
    uint32_t miplevel, uint32_t arrayslice
);

/* === Surface generation === */
GpaError sceGpaFindOptimalSurface(
    GpaSurfaceProperties* outprops, GpaSurfaceType surfacetype, uint32_t bpp,
    uint32_t numfrags, bool mipmapped, GnmGpuMode mingpumode
);

/* === Element / utility === */
GpaError sceGpaGetTileInfo(
    GpaTileInfo* outinfo, GnmTileMode tilemode, uint32_t bpp, uint32_t numfrags,
    GnmGpuMode gpumode
);
GpaError sceGpaComputeBaseSwizzle(
    uint32_t* outswizzle, GnmTileMode tilemode, uint32_t surfindex,
    uint32_t bpp, uint32_t numfrags, GnmGpuMode gpumode
);

/* === Decompression === */
GpaError sceGpaGetDecompressedSize(
    uint64_t* outlen, const GpaTextureInfo* texinfo
);
GpaError sceGpaDecompressTexture(
    void* outbuf, uint64_t outlen, const void* inbuf, uint64_t inlen,
    const GpaTextureInfo* texinfo, GnmDataFormat* outfmt
);

/* === Tiler === */
GpaError sceGpaTpInit(
    GpaTilingParams* tp, const GpaTextureInfo* tex, uint32_t miplevel,
    uint32_t arrayslice
);

GpaError sceGpaTileSurface(
    void* outbuf, size_t outlen, const void* inbuf, size_t inlen,
    const GpaTilingParams* srctp, const GpaTilingParams* dst_tp
);
GpaError sceGpaTileSurfaceRegion(
    void* outbuf, size_t outlen, const void* inbuf, size_t inlen,
    const GpaTilingParams* srctp, const GpaTilingParams* dst_tp,
    const GpaSurfaceRegion* region
);
GpaError sceGpaTileTextureIndexed(
    const void* inbuf, size_t inlen, void* outbuf, size_t outlen,
    const GpaTextureInfo* texinfo, GnmTileMode newtiling, uint32_t mip,
    uint32_t slice
);
GpaError sceGpaTileTextureAll(
    const void* inbuf, size_t inlen, void* outbuf, size_t outlen,
    const GpaTextureInfo* texinfo, GnmTileMode newtiling
);

OPENGNM_EXTERN_C_END

#endif /* _GPUADDR_H_ */
