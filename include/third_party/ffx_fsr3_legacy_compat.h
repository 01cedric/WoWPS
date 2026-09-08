#pragma once

#include <ffx_fsr3upscaler.h>
#include <ffx_frameinterpolation.h>
#include <ffx_opticalflow.h>
#include <ffx_framegeneration.h>

// Kits SDK uses FfxApi* type names. Preserve the legacy aliases expected by
// the existing runtime/wrapper code so we can compile against either layout.
using FfxSurfaceFormat = FfxApiSurfaceFormat;
using FfxDimensions2D = FfxApiDimensions2D;
using FfxFloatCoords2D = FfxApiFloatCoords2D;
using FfxResource = FfxApiResource;
using FfxResourceDescription = FfxApiResourceDescription;
using FfxResourceUsage = FfxApiResourceUsage;

#ifndef FFX_FSR3_CONTEXT_SIZE
#define FFX_FSR3_CONTEXT_SIZE (FFX_SDK_DEFAULT_CONTEXT_SIZE)
#endif

#ifndef FFX_FSR3_CONTEXT_COUNT
// Combined FSR3 path uses shared + upscaler + frame interpolation contexts.
#define FFX_FSR3_CONTEXT_COUNT (3)
#endif

#ifndef FFX_RESOURCE_TYPE_TEXTURE2D
#define FFX_RESOURCE_TYPE_TEXTURE2D FFX_API_RESOURCE_TYPE_TEXTURE2D
#endif
#ifndef FFX_RESOURCE_FLAGS_NONE
#define FFX_RESOURCE_FLAGS_NONE FFX_API_RESOURCE_FLAGS_NONE
#endif
#ifndef FFX_RESOURCE_USAGE_READ_ONLY
#define FFX_RESOURCE_USAGE_READ_ONLY FFX_API_RESOURCE_USAGE_READ_ONLY
#endif
#ifndef FFX_RESOURCE_USAGE_UAV
#define FFX_RESOURCE_USAGE_UAV FFX_API_RESOURCE_USAGE_UAV
#endif
#ifndef FFX_RESOURCE_USAGE_DEPTHTARGET
#define FFX_RESOURCE_USAGE_DEPTHTARGET FFX_API_RESOURCE_USAGE_DEPTHTARGET
#endif
#ifndef FFX_RESOURCE_STATE_COMPUTE_READ
#define FFX_RESOURCE_STATE_COMPUTE_READ FFX_API_RESOURCE_STATE_COMPUTE_READ
#endif
#ifndef FFX_RESOURCE_STATE_UNORDERED_ACCESS
#define FFX_RESOURCE_STATE_UNORDERED_ACCESS FFX_API_RESOURCE_STATE_UNORDERED_ACCESS
#endif

#ifndef FFX_SURFACE_FORMAT_UNKNOWN
#define FFX_SURFACE_FORMAT_UNKNOWN FFX_API_SURFACE_FORMAT_UNKNOWN
#define FFX_SURFACE_FORMAT_R32_FLOAT FFX_API_SURFACE_FORMAT_R32_FLOAT
#define FFX_SURFACE_FORMAT_R16_UNORM FFX_API_SURFACE_FORMAT_R16_UNORM
#define FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT
#define FFX_SURFACE_FORMAT_R8G8B8A8_UNORM FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM
#define FFX_SURFACE_FORMAT_R8G8B8A8_SRGB FFX_API_SURFACE_FORMAT_R8G8B8A8_SRGB
#define FFX_SURFACE_FORMAT_R10G10B10A2_UNORM FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM
#define FFX_SURFACE_FORMAT_R11G11B10_FLOAT FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT
#define FFX_SURFACE_FORMAT_R16G16_FLOAT FFX_API_SURFACE_FORMAT_R16G16_FLOAT
#define FFX_SURFACE_FORMAT_R16G16_UINT FFX_API_SURFACE_FORMAT_R16G16_UINT
#define FFX_SURFACE_FORMAT_R16_FLOAT FFX_API_SURFACE_FORMAT_R16_FLOAT
#define FFX_SURFACE_FORMAT_R16_UINT FFX_API_SURFACE_FORMAT_R16_UINT
#define FFX_SURFACE_FORMAT_R16_SNORM FFX_API_SURFACE_FORMAT_R16_SNORM
#define FFX_SURFACE_FORMAT_R8_UNORM FFX_API_SURFACE_FORMAT_R8_UNORM
#define FFX_SURFACE_FORMAT_R8_UINT FFX_API_SURFACE_FORMAT_R8_UINT
#define FFX_SURFACE_FORMAT_R8G8_UNORM FFX_API_SURFACE_FORMAT_R8G8_UNORM
#define FFX_SURFACE_FORMAT_R32_UINT FFX_API_SURFACE_FORMAT_R32_UINT
#endif

#ifndef FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB
#define FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB
#define FFX_BACKBUFFER_TRANSFER_FUNCTION_SCRGB FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SCRGB
#endif

enum FfxFsr3InitializationFlagBitsCompat {
    FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE = (1u << 0),
    FFX_FSR3_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS = (1u << 1),
    FFX_FSR3_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION = (1u << 2),
    FFX_FSR3_ENABLE_DEPTH_INVERTED = (1u << 3),
    FFX_FSR3_ENABLE_DEPTH_INFINITE = (1u << 4),
    FFX_FSR3_ENABLE_AUTO_EXPOSURE = (1u << 5),
    FFX_FSR3_ENABLE_DYNAMIC_RESOLUTION = (1u << 6),
    FFX_FSR3_ENABLE_UPSCALING_ONLY = (1u << 7)
};

typedef struct FfxFsr3ContextDescription {
    uint32_t flags;
    FfxApiDimensions2D maxRenderSize;
    FfxApiDimensions2D displaySize;
    FfxApiDimensions2D upscaleOutputSize;
    FfxFsr3UpscalerMessage fpMessage;
    FfxInterface backendInterfaceSharedResources;
    FfxInterface backendInterfaceUpscaling;
    FfxInterface backendInterfaceFrameInterpolation;
    FfxApiSurfaceFormat backBufferFormat;
} FfxFsr3ContextDescription;

typedef struct FfxFsr3Context {
    uint32_t data[FFX_FSR3_CONTEXT_SIZE];
} FfxFsr3Context;

typedef struct FfxFsr3DispatchUpscaleDescription {
    FfxCommandList commandList;
    FfxApiResource color;
    FfxApiResource depth;
    FfxApiResource motionVectors;
    FfxApiResource exposure;
    FfxApiResource reactive;
    FfxApiResource transparencyAndComposition;
    FfxApiResource upscaleOutput;
    FfxApiFloatCoords2D jitterOffset;
    FfxApiFloatCoords2D motionVectorScale;
    FfxApiDimensions2D renderSize;
    bool enableSharpening;
    float sharpness;
    float frameTimeDelta;
    float preExposure;
    bool reset;
    float cameraNear;
    float cameraFar;
    float cameraFovAngleVertical;
    float viewSpaceToMetersFactor;
} FfxFsr3DispatchUpscaleDescription;

typedef struct FfxFrameGenerationDispatchDescription {
    FfxCommandList commandList;
    FfxApiResource presentColor;
    FfxApiResource outputs[4];
    uint32_t numInterpolatedFrames;
    bool reset;
    uint32_t backBufferTransferFunction;
    float minMaxLuminance[2];
} FfxFrameGenerationDispatchDescription;

#if defined(__cplusplus)
extern "C" {
#endif
FFX_API FfxErrorCode ffxFsr3ContextCreate(FfxFsr3Context* context, const FfxFsr3ContextDescription* contextDescription);
FFX_API FfxErrorCode ffxFsr3ContextDispatchUpscale(FfxFsr3Context* context, const FfxFsr3DispatchUpscaleDescription* dispatchDescription);
FFX_API FfxErrorCode ffxFsr3ConfigureFrameGeneration(FfxFsr3Context* context, const FfxFrameGenerationConfig* config);
FFX_API FfxErrorCode ffxFsr3DispatchFrameGeneration(const FfxFrameGenerationDispatchDescription* dispatchDescription);
FFX_API FfxErrorCode ffxFsr3ContextDestroy(FfxFsr3Context* context);
#if defined(__cplusplus)
}
#endif
