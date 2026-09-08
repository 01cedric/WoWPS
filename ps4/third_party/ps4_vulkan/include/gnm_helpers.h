#ifndef _GNM_HELPERS_H_
#define _GNM_HELPERS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gnm_commandbuffer.h"
#include "gnm_error.h"
#include "gnm_rendertarget.h"
#include "gnm_shaderbinary.h"
#include "gnm_texture.h"

OPENGNM_EXTERN_C_BEGIN

#define GNM_DIRECT_MEMORY_TYPE_WC_GARLIC 3

#define GNM_PROT_CPU_READ 0x01
#define GNM_PROT_CPU_RW 0x02
#define GNM_PROT_GPU_READ 0x10
#define GNM_PROT_GPU_WRITE 0x20
#define GNM_PROT_CPU_GPU_RW \
	(GNM_PROT_CPU_READ | GNM_PROT_CPU_RW | GNM_PROT_GPU_READ | GNM_PROT_GPU_WRITE)

#define GNM_VIDEO_OUT_MAX_BUFFERS 4
#define GNM_VIDEO_OUT_DEFAULT_WIDTH 1920
#define GNM_VIDEO_OUT_DEFAULT_HEIGHT 1080
#define GNM_VIDEO_OUT_DEFAULT_BUFFERS 2
#define GNM_VIDEO_OUT_BYTES_PER_PIXEL_A8B8G8R8 4
#define GNM_VIDEO_OUT_MEMORY_ALIGNMENT (64u * 1024u)
#define GNM_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB 0x80002200u
#define GNM_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_LEGACY 0x80000000u
#define GNM_VIDEO_OUT_TILING_MODE_LINEAR 1
#define GNM_VIDEO_OUT_ASPECT_RATIO_NONE 0
#define GNM_VIDEO_OUT_BUS_MAIN 0
#define GNM_VIDEO_OUT_FLIP_60HZ 0
#define GNM_VIDEO_OUT_FLIP_VSYNC 1

typedef struct {
	void* mapped;
	uint64_t directmemory;
	uint64_t size;
	uint64_t alignment;
	void* allocation;
	bool allocated;
} GnmDirectMemory;

GnmError PS4_SYSV_ABI sceGnmDirectMemoryAllocate(
    GnmDirectMemory* memory, uint64_t size, uint64_t alignment,
    int32_t memorytype, int32_t protection
);
void PS4_SYSV_ABI sceGnmDirectMemoryRelease(GnmDirectMemory* memory);

typedef struct {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t numbuffers;
	uint32_t bytesperpixel;
	uint64_t alignment;
	int32_t bus;
	int32_t pixel_format;
	int32_t tiling_mode;
	int32_t aspect_ratio;
	int32_t flip_rate;
} GnmVideoOutCreateInfo;

typedef struct {
	int32_t handle;
	uintptr_t flipqueue;
	GnmDirectMemory memory;
	void* buffers[GNM_VIDEO_OUT_MAX_BUFFERS];
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t numbuffers;
	uint32_t registeredbuffers;
	uint32_t currentbuffer;
	uint64_t buffersize;
	uint64_t bufferstride;
	uint64_t frame;
	/* Pixel format that sceVideoOutRegisterBuffers accepted. */
	uint32_t registered_pixel_format;
	/* Diagnostic stage for open/flip failures (1..8, zero on success). */
	uint32_t last_error_stage;
	int32_t last_error_code;
} GnmVideoOut;

void PS4_SYSV_ABI sceGnmVideoOutInitDefaultCreateInfo(
    GnmVideoOutCreateInfo* info, uint32_t width, uint32_t height
);
GnmError PS4_SYSV_ABI sceGnmVideoOutCalcBufferLayout(
    const GnmVideoOutCreateInfo* info, uint64_t* outbuffersize,
    uint64_t* outbufferstride
);
GnmError PS4_SYSV_ABI sceGnmVideoOutOpen(
    GnmVideoOut* videoout, const GnmVideoOutCreateInfo* info
);
void PS4_SYSV_ABI sceGnmVideoOutClose(GnmVideoOut* videoout);
void* PS4_SYSV_ABI sceGnmVideoOutGetBuffer(
    const GnmVideoOut* videoout, uint32_t bufferindex
);
GnmError PS4_SYSV_ABI sceGnmVideoOutSubmitFlipAndWait(
    GnmVideoOut* videoout, uint32_t bufferindex, int64_t fliparg,
    int32_t flipmode
);

void PS4_SYSV_ABI sceGnmTexInit2dCreateInfo(
    GnmTextureCreateInfo* info, GnmDataFormat format, uint32_t width,
    uint32_t height, uint32_t mipcount, GnmTileMode tilemode,
    GnmGpuMode mingpumode
);
GnmError PS4_SYSV_ABI sceGnmTexCreate2d(
    GnmTexture* texture, void* baseaddr, GnmDataFormat format,
    uint32_t width, uint32_t height, uint32_t mipcount,
    GnmTileMode tilemode, GnmGpuMode mingpumode, uint64_t* outsize,
    uint32_t* outalignment
);

void PS4_SYSV_ABI sceGnmRtInitColorTargetCreateInfo(
    GnmRenderTargetCreateInfo* info, GnmDataFormat format, uint32_t width,
    uint32_t height, uint32_t numslices, uint32_t numsamples,
    uint32_t numfragments, GnmTileMode tilemode, GnmGpuMode mingpumode
);
GnmError PS4_SYSV_ABI sceGnmRtCreateColorTarget(
    GnmRenderTarget* rt, void* baseaddr, GnmDataFormat format,
    uint32_t width, uint32_t height, uint32_t numslices,
    uint32_t numsamples, uint32_t numfragments, GnmTileMode tilemode,
    GnmGpuMode mingpumode, uint64_t* outsize, uint32_t* outalignment
);

typedef struct {
	GnmShaderType type;
	GnmTargetGpuMode targetgpumodes;
	uint16_t versionmajor;
	uint16_t versionminor;
	const GnmShaderFileHeader* fileheader;
	const GnmShaderCommonData* common;
	const void* stage;
	uint32_t stagesize;
	const GnmInputUsageSlot* inputusageslots;
	uint32_t numinputusageslots;
	uint32_t numinputsemantics;
	uint32_t numexportsemantics;
	const void* shadercode;
	uint32_t shadercodesize;
} GnmShaderMetadata;

GnmError PS4_SYSV_ABI sceGnmShaderBinaryGetMetadata(
    const void* data, size_t size, GnmShaderMetadata* outmetadata
);

typedef struct {
	uint32_t capacitydwords;
	uint32_t useddwords;
	uint32_t remainingdwords;
	const char* message;
} GnmCommandBufferValidationInfo;

GnmError PS4_SYSV_ABI sceGnmCmdValidate(
    const GnmCommandBuffer* cmd, GnmCommandBufferValidationInfo* outinfo
);

OPENGNM_EXTERN_C_END

#endif /* _GNM_HELPERS_H_ */
