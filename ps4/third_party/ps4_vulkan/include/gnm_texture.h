#ifndef _GNM_TEXTURE_H_
#define _GNM_TEXTURE_H_

#include <stdint.h>

#include "gnm_dataformat.h"
#include "gnm_error.h"
#include "gnm_types.h"
#include "pm4/gpuaddr_types.h"

OPENGNM_EXTERN_C_BEGIN

typedef struct {
	GnmDataFormat format;
	GnmTextureType texturetype;
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint32_t pitch;

	uint32_t nummiplevels;
	uint32_t numslices;
	uint32_t numfragments;

	GnmTileMode tilemodehint;
	GnmGpuMode mingpumode;
} GnmTextureCreateInfo;

typedef struct {
	uint32_t baseaddress;

	uint32_t baseaddresshi : 6;
	uint32_t mtype_l2 : 2;
	uint32_t minlod : 12;
	GnmImageFormat dataformat : 6;
	GnmImgNumFormat numformat : 4;
	uint32_t mtype0 : 2;

	uint32_t width : 14;
	uint32_t height : 14;
	uint32_t perfmod : 3;
	uint32_t interlaced : 1;

	GnmChannel dstselx : 3;
	GnmChannel dstsely : 3;
	GnmChannel dstselz : 3;
	GnmChannel dstselw : 3;
	uint32_t baselevel : 4;
	uint32_t lastlevel : 4;
	GnmTileMode tilingindex : 5;
	uint32_t pow2pad : 1;
	uint32_t mtype2 : 1;
	uint32_t atc : 1;
	GnmTextureType type : 4;

	uint32_t depth : 13;
	uint32_t pitch : 14;
	uint32_t _unused : 5;

	uint32_t basearray : 13;
	uint32_t lastarray : 13;
	uint32_t _unused2 : 6;

	uint32_t minlodwarn : 12;
	uint32_t counterbankid : 8;
	uint32_t lodhdwcnten : 1;
	uint32_t compressionen : 1;
	uint32_t alphaisonmsb : 1;
	uint32_t colortransform : 1;
	uint32_t alttilemode : 1;
	uint32_t _unused3 : 7;

	uint32_t metadataaddr;
} GnmTexture;
_Static_assert(sizeof(GnmTexture) == 0x20, "");

GnmError sceGnmCreateTexture(
    GnmTexture* tex, const GnmTextureCreateInfo* createinfo
);

static inline void* sceGnmTexGetBaseAddress(const GnmTexture* tex) {
	return (void*)(((uintptr_t)tex->baseaddress << 8) |
	               ((uintptr_t)tex->baseaddresshi << 40));
}
void sceGnmTexSetBaseAddress(GnmTexture* tex, void* baseaddr);

static inline GnmDataFormat sceGnmTexGetFormat(const GnmTexture* tex) {
	GnmDataFormat fmt;
	fmt.asuint = 0;
	fmt.surfacefmt = tex->dataformat;
	fmt.chantype = tex->numformat;
	fmt.chanx = tex->dstselx;
	fmt.chany = tex->dstsely;
	fmt.chanz = tex->dstselz;
	fmt.chanw = tex->dstselw;
	return fmt;
}
static inline void sceGnmTexSetFormat(GnmTexture* tex, GnmDataFormat df) {
	tex->dataformat = df.surfacefmt;
	tex->numformat = df.chantype;
	tex->dstselx = df.chanx;
	tex->dstsely = df.chany;
	tex->dstselz = df.chanz;
	tex->dstselw = df.chanw;
}

static inline uint32_t sceGnmTexGetWidth(const GnmTexture* tex) {
	return tex->width + 1;
}
static inline void sceGnmTexSetWidth(GnmTexture* tex, uint32_t width) {
	tex->width = width - 1;
}
static inline uint32_t sceGnmTexGetHeight(const GnmTexture* tex) {
	return tex->height + 1;
}
static inline void sceGnmTexSetHeight(GnmTexture* tex, uint32_t height) {
	tex->height = height - 1;
}
static inline uint32_t sceGnmTexGetDepth(const GnmTexture* tex) {
	return tex->depth + 1;
}
static inline void sceGnmTexSetDepth(GnmTexture* tex, uint32_t depth) {
	tex->depth = depth - 1;
}
static inline uint32_t sceGnmTexGetPitch(const GnmTexture* tex) {
	return tex->pitch + 1;
}
static inline void sceGnmTexSetPitch(GnmTexture* tex, uint32_t pitch) {
	tex->pitch = pitch - 1;
}

static inline uint32_t sceGnmTexGetBaseMipLevel(const GnmTexture* tex) {
	if (tex->type == GNM_TEXTURE_2D_MSAA ||
	    tex->type == GNM_TEXTURE_2D_ARRAY_MSAA) {
		return 0;
	}
	return tex->baselevel;
}
static inline uint32_t sceGnmTexGetLastMipLevel(const GnmTexture* tex) {
	if (tex->type == GNM_TEXTURE_2D_MSAA ||
	    tex->type == GNM_TEXTURE_2D_ARRAY_MSAA) {
		return 0;
	}
	return tex->lastlevel;
}
static inline uint32_t sceGnmTexGetNumMips(const GnmTexture* tex) {
	return sceGnmTexGetLastMipLevel(tex) + 1;
}
static inline uint32_t sceGnmTexGetNumFaces(const GnmTexture* tex) {
	return tex->type == GNM_TEXTURE_CUBEMAP ? 6 : 1;
}
static inline uint32_t sceGnmTexGetTotalArraySlices(const GnmTexture* tex) {
	return tex->type == GNM_TEXTURE_3D ? 1 : sceGnmTexGetDepth(tex);
}
uint32_t sceGnmTexGetNumArraySlices(const GnmTexture* tex);

static inline uint8_t sceGnmTexGetNumFragments(const GnmTexture* tex) {
	if (tex->type == GNM_TEXTURE_2D_MSAA ||
	    tex->type == GNM_TEXTURE_2D_ARRAY_MSAA) {
		return 1 << tex->lastlevel;
	}
	return 1;
}

static inline void sceGnmTexSetMemoryType(
    GnmTexture* tex, GnmMemoryType memtype, bool l1cachebypass
) {
	tex->mtype_l2 = memtype & 0x3;
	tex->mtype0 = !l1cachebypass;
	tex->mtype2 = (memtype & GNM_MEMORY_READONLY) == GNM_MEMORY_READONLY;
}

static inline GpaTextureInfo sceGnmTexBuildInfo(const GnmTexture* tex) {
	GpaTextureInfo info;
	info.type = (GnmTextureType)tex->type;
	info.fmt = sceGnmTexGetFormat(tex);
	info.width = sceGnmTexGetWidth(tex);
	info.height = sceGnmTexGetHeight(tex);
	info.pitch = sceGnmTexGetPitch(tex);
	info.depth = sceGnmTexGetDepth(tex);
	info.numfrags = sceGnmTexGetNumFragments(tex);
	info.nummips = sceGnmTexGetNumMips(tex);
	info.numslices = sceGnmTexGetNumArraySlices(tex);
	info.tm = (GnmTileMode)tex->tilingindex;
	info.mingpumode = tex->alttilemode ? GNM_GPU_NEO : GNM_GPU_BASE;
	info.pow2pad = tex->pow2pad;
	return info;
}

GnmError sceGnmTexCalcByteSize(
    uint64_t* outsize, uint32_t* outalignment, const GnmTexture* tex
);

OPENGNM_EXTERN_C_END

#endif /* _GNM_TEXTURE_H_ */
