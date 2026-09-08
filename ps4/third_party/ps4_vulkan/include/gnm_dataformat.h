#ifndef _GNM_DATAFORMAT_H_
#define _GNM_DATAFORMAT_H_

#include <stdbool.h>

#include "gnm_types.h"

OPENGNM_EXTERN_C_BEGIN

typedef union {
	struct {
		GnmImageFormat surfacefmt : 8;
		GnmImgNumFormat chantype : 4;
		GnmChannel chanx : 3;
		GnmChannel chany : 3;
		GnmChannel chanz : 3;
		GnmChannel chanw : 3;
		uint32_t _unused : 8;
	};
	uint32_t asuint;
} GnmDataFormat;
_Static_assert(sizeof(GnmDataFormat) == 0x4, "");

#define GNM_DATA_FORMAT_INIT(_surfacefmt, _chantype, _chanx, _chany, _chanz, _chanw) \
	{{(_surfacefmt), (_chantype), (_chanx), (_chany), (_chanz), (_chanw), 0}}

GnmDataFormat sceGnmDfInitFromFmask(uint32_t numsamples, uint32_t numfrags);
GnmDataFormat sceGnmDfInitFromZ(GnmZFormat zfmt);

static inline GnmDataFormat sceGnmDfInitFromStencil(
    GnmStencilFormat stencilfmt, GnmImgNumFormat chantype
) {
	GnmDataFormat res =
	    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_INVALID, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_X, GNM_CHAN_X, GNM_CHAN_X);
	res.surfacefmt = stencilfmt == GNM_STENCIL_8
			     ? GNM_IMG_DATA_FORMAT_8
			     : GNM_IMG_DATA_FORMAT_INVALID;
	res.chantype = chantype;
	res.chanx = GNM_CHAN_X;
	res.chany = GNM_CHAN_X;
	res.chanz = GNM_CHAN_X;
	res.chanw = GNM_CHAN_X;
	return res;
}

static inline uint32_t sceGnmDfGetTexelsPerElement(const GnmDataFormat datafmt) {
	switch (datafmt.surfacefmt) {
	case GNM_IMG_DATA_FORMAT_BC1:
	case GNM_IMG_DATA_FORMAT_BC2:
	case GNM_IMG_DATA_FORMAT_BC3:
	case GNM_IMG_DATA_FORMAT_BC4:
	case GNM_IMG_DATA_FORMAT_BC5:
	case GNM_IMG_DATA_FORMAT_BC6:
	case GNM_IMG_DATA_FORMAT_BC7:
		return 16;
	case GNM_IMG_DATA_FORMAT_1:
	case GNM_IMG_DATA_FORMAT_1_REVERSED:
		return 8;
	default:
		return 1;
	}
}

uint32_t sceGnmDfGetNumComponents(const GnmDataFormat datafmt);
uint32_t sceGnmDfGetBitsPerElement(const GnmDataFormat datafmt);

static inline uint32_t sceGnmDfGetTotalBitsPerElement(const GnmDataFormat fmt) {
	const uint32_t bitsperelem = sceGnmDfGetBitsPerElement(fmt);
	const uint32_t texelsperelem = sceGnmDfGetTexelsPerElement(fmt);
	return bitsperelem * texelsperelem;
}

static inline uint32_t sceGnmDfGetBytesPerElement(const GnmDataFormat datafmt) {
	return sceGnmDfGetBitsPerElement(datafmt) / 8;
}

static inline uint32_t sceGnmDfGetTotalBytesPerElement(const GnmDataFormat fmt) {
	return sceGnmDfGetTotalBitsPerElement(fmt) / 8;
}

static inline bool sceGnmDfIsBlockCompressed(const GnmDataFormat datafmt) {
	switch (datafmt.surfacefmt) {
	case GNM_IMG_DATA_FORMAT_BC1:
	case GNM_IMG_DATA_FORMAT_BC2:
	case GNM_IMG_DATA_FORMAT_BC3:
	case GNM_IMG_DATA_FORMAT_BC4:
	case GNM_IMG_DATA_FORMAT_BC5:
	case GNM_IMG_DATA_FORMAT_BC6:
	case GNM_IMG_DATA_FORMAT_BC7:
		return true;
	default:
		return false;
	}
}

bool sceGnmDfGetRtChannelType(const GnmDataFormat datafmt, GnmSurfaceNumber* out);
bool sceGnmDfGetRtChannelOrder(const GnmDataFormat datafmt, GnmSurfaceSwap* out);

GnmZFormat sceGnmDfGetZFormat(const GnmDataFormat datafmt);
GnmStencilFormat sceGnmDfGetStencilFormat(const GnmDataFormat datafmt);

static inline uint32_t sceGnmDfGetTexelsPerElementWide(const GnmDataFormat fmt) {
	switch (fmt.surfacefmt) {
	case GNM_IMG_DATA_FORMAT_BC1:
	case GNM_IMG_DATA_FORMAT_BC2:
	case GNM_IMG_DATA_FORMAT_BC3:
	case GNM_IMG_DATA_FORMAT_BC4:
	case GNM_IMG_DATA_FORMAT_BC5:
	case GNM_IMG_DATA_FORMAT_BC6:
	case GNM_IMG_DATA_FORMAT_BC7:
		return 4;
	case GNM_IMG_DATA_FORMAT_1:
	case GNM_IMG_DATA_FORMAT_1_REVERSED:
		return 8;
	case GNM_IMG_DATA_FORMAT_GB_GR:
	case GNM_IMG_DATA_FORMAT_BG_RG:
		return 2;
	default:
		return 1;
	}
}

static inline uint32_t sceGnmDfGetTexelsPerElementTall(const GnmDataFormat fmt) {
	switch (fmt.surfacefmt) {
	case GNM_IMG_DATA_FORMAT_BC1:
	case GNM_IMG_DATA_FORMAT_BC2:
	case GNM_IMG_DATA_FORMAT_BC3:
	case GNM_IMG_DATA_FORMAT_BC4:
	case GNM_IMG_DATA_FORMAT_BC5:
	case GNM_IMG_DATA_FORMAT_BC6:
	case GNM_IMG_DATA_FORMAT_BC7:
		return 4;
	default:
		return 1;
	}
}

/* Predefined format constants */
static const GnmDataFormat GNM_FMT_INVALID =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_INVALID, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT0);
static const GnmDataFormat GNM_FMT_R8_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_8, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT1);
static const GnmDataFormat GNM_FMT_A8_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_8, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT0, GNM_CHAN_X);
static const GnmDataFormat GNM_FMT_R8G8B8A8_SRGB =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_8_8_8_8, GNM_IMG_NUM_FORMAT_SRGB, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_R8G8B8A8_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_8_8_8_8, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_R8G8B8A8_UINT =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_8_8_8_8, GNM_IMG_NUM_FORMAT_UINT, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_B8G8R8A8_SRGB =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_8_8_8_8, GNM_IMG_NUM_FORMAT_SRGB, GNM_CHAN_Z, GNM_CHAN_Y, GNM_CHAN_X, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_B8G8R8A8_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_8_8_8_8, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_Z, GNM_CHAN_Y, GNM_CHAN_X, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_R16_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_16, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT1);
static const GnmDataFormat GNM_FMT_R16G16_FLOAT =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_16_16, GNM_IMG_NUM_FORMAT_FLOAT, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT1);
static const GnmDataFormat GNM_FMT_R16G16B16A16_SRGB =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_16_16_16_16, GNM_IMG_NUM_FORMAT_SRGB, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_R16G16B16A16_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_16_16_16_16, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_R16G16B16A16_FLOAT =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_16_16_16_16, GNM_IMG_NUM_FORMAT_FLOAT, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_R32_FLOAT =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_32, GNM_IMG_NUM_FORMAT_FLOAT, GNM_CHAN_X, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT1);
static const GnmDataFormat GNM_FMT_R32G32_FLOAT =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_32_32, GNM_IMG_NUM_FORMAT_FLOAT, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_CONSTANT0, GNM_CHAN_CONSTANT1);
static const GnmDataFormat GNM_FMT_R32G32B32_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_32_32_32, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_CONSTANT0);
static const GnmDataFormat GNM_FMT_R32G32B32_FLOAT =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_32_32_32, GNM_IMG_NUM_FORMAT_FLOAT, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_CONSTANT1);
static const GnmDataFormat GNM_FMT_R32G32B32A32_SRGB =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_32_32_32_32, GNM_IMG_NUM_FORMAT_SRGB, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_R32G32B32A32_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_32_32_32_32, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_R32G32B32A32_FLOAT =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_32_32_32_32, GNM_IMG_NUM_FORMAT_FLOAT, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_BC1_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_BC1, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_BC1_SRGB =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_BC1, GNM_IMG_NUM_FORMAT_SRGB, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_BC3_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_BC3, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_BC6_SNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_BC6, GNM_IMG_NUM_FORMAT_SNORM, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_CONSTANT1);
static const GnmDataFormat GNM_FMT_BC6_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_BC6, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_CONSTANT1);
static const GnmDataFormat GNM_FMT_BC7_UNORM =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_BC7, GNM_IMG_NUM_FORMAT_UNORM, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);
static const GnmDataFormat GNM_FMT_BC7_SRGB =
    GNM_DATA_FORMAT_INIT(GNM_IMG_DATA_FORMAT_BC7, GNM_IMG_NUM_FORMAT_SRGB, GNM_CHAN_X, GNM_CHAN_Y, GNM_CHAN_Z, GNM_CHAN_W);

OPENGNM_EXTERN_C_END

#endif /* _GNM_DATAFORMAT_H_ */
