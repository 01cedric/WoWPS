#ifndef _GNM_BUFFER_H_
#define _GNM_BUFFER_H_

#include <stdbool.h>

#include "gnm_dataformat.h"
#include "gnm_error.h"
#include "gnm_types.h"

typedef struct {
	/* register 0 */
	uint32_t baseaddress;

	/* register 1 */
	uint32_t baseaddresshi : 12;
	uint32_t mtype_l1s : 2;
	uint32_t mtype_l2 : 2;
	uint32_t stride : 14;
	uint32_t cacheswizzle : 1;
	uint32_t swizzleen : 1;

	/* register 2 */
	uint32_t numrecords;

	/* register 3 */
	GnmChannel dstselx : 3;
	GnmChannel dstsely : 3;
	GnmChannel dstselz : 3;
	GnmChannel dstselw : 3;
	GnmBufNumFormat numformat : 3;
	GnmBufferFormat dataformat : 4;
	uint32_t elementsize : 2;
	uint32_t indexstride : 2;
	uint32_t addtiden : 1;
	uint32_t atc : 1;
	uint32_t hashen : 1;
	uint32_t heap : 1;
	uint32_t mtype : 3;
	uint32_t type : 2;
} GnmBuffer;
_Static_assert(sizeof(GnmBuffer) == 0x10, "");

static inline void* sceGnmBufGetBaseAddress(const GnmBuffer* buf) {
	return (void*)((uintptr_t)buf->baseaddress |
		       ((uintptr_t)buf->baseaddresshi << 32));
}

static inline void sceGnmBufSetBaseAddress(GnmBuffer* buf, void* baseaddr) {
	if ((uintptr_t)baseaddr & 3) {
		sceGnmWriteMsg(
		    GNM_MSGSEV_ERR,
		    "sceGnmBuf: baseaddr must be aligned to 4 bytes"
		);
		return;
	}
	buf->baseaddress = (uintptr_t)baseaddr & 0xffffffff;
	buf->baseaddresshi = ((uintptr_t)baseaddr >> 32) & 0xfff;
}

static inline GnmDataFormat sceGnmBufGetFormat(const GnmBuffer* buf) {
	GnmDataFormat fmt;
	fmt.asuint = 0;
	fmt.surfacefmt = (GnmImageFormat)buf->dataformat;
	fmt.chantype = (GnmImgNumFormat)buf->numformat;
	fmt.chanx = buf->dstselx;
	fmt.chany = buf->dstsely;
	fmt.chanz = buf->dstselz;
	fmt.chanw = buf->dstselw;
	return fmt;
}

static inline void sceGnmBufSetFormat(GnmBuffer* buf, GnmDataFormat fmt) {
	if (fmt.chantype < (int)GNM_BUF_NUM_FORMAT_UNORM ||
	    fmt.chantype > (int)GNM_BUF_NUM_FORMAT_FLOAT ||
	    fmt.surfacefmt <= (int)GNM_BUF_DATA_FORMAT_INVALID ||
	    fmt.surfacefmt > (int)GNM_BUF_DATA_FORMAT_32_32_32_32) {
		sceGnmWriteMsg(
		    GNM_MSGSEV_ERR, "Buffer: data format is unsupported"
		);
		return;
	}
	buf->numformat = (GnmBufNumFormat)fmt.chantype;
	buf->dataformat = (GnmBufferFormat)fmt.surfacefmt;
	buf->dstselx = fmt.chanx;
	buf->dstsely = fmt.chany;
	buf->dstselz = fmt.chanz;
	buf->dstselw = fmt.chanw;
}

static inline void sceGnmBufSetMemoryType(
    GnmBuffer* buf, GnmMemoryType memtype, bool l1cachebypass, bool kcachebypass
) {
	buf->mtype = (!l1cachebypass ? 0x0 : 0x3) | ((memtype >> 2) & 0x4);
	buf->mtype_l1s = (!kcachebypass ? 0x0 : 0x3) | ((memtype >> 5) & 0x3);
	buf->mtype_l2 = memtype & 0x3;
}

static inline GnmBuffer sceGnmCreateConstBuffer(
    void* baseaddr, uint32_t bytesize
) {
	GnmBuffer res = {0};
	sceGnmBufSetBaseAddress(&res, baseaddr);
	res.stride = 16;
	res.numrecords = (bytesize + 15) / 16;
	sceGnmBufSetFormat(&res, GNM_FMT_R32G32B32A32_FLOAT);
	sceGnmBufSetMemoryType(&res, GNM_MEMORY_READONLY, false, false);
	return res;
}

static inline GnmBuffer sceGnmCreateVertexBuffer(
    void* baseaddr, GnmDataFormat fmt, uint32_t stride, uint32_t numelements
) {
	const uint32_t elementbytesize = sceGnmDfGetBytesPerElement(fmt);
	const uint32_t minalignment =
	    (elementbytesize > 4 ? elementbytesize : 4) - 1;
	if ((uintptr_t)baseaddr & minalignment) {
		sceGnmWriteMsgf(
		    GNM_MSGSEV_ERR, "sceGnmBuf: baseaddr must be aligned to %u",
		    minalignment
		);
	}

	if (stride == 0 && numelements != elementbytesize) {
		sceGnmWriteMsgf(
		    GNM_MSGSEV_ERR,
		    "sceGnmBuf: if stride is 0, numelements (%u) must be format's "
		    "size (%u)",
		    numelements, elementbytesize
		);
	}

	GnmBuffer res = {0};
	sceGnmBufSetBaseAddress(&res, baseaddr);
	res.stride = stride;
	res.numrecords = numelements;
	sceGnmBufSetFormat(&res, fmt);
	sceGnmBufSetMemoryType(&res, GNM_MEMORY_READONLY, false, false);
	return res;
}

#endif /* _GNM_BUFFER_H_ */
