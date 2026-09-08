/*
 * vk_ps4_instance.c — VkInstance / VkPhysicalDevice implementation.
 */

#include "vk_ps4_internal.h"
#include "vk_ps4_cache_abi.h"

#include <string.h>

static const char *g_device_name = "PS4 GPU (GNM)";
static const char *g_driver_name = "vulkan-ps4";
static const char *g_driver_info = "Vulkan 1.0 ICD over OpenGNM";

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateInstance(
    const VkInstanceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkInstance *pInstance
) {
    /* Open breadcrumb log on first instance creation if not already open */
    if (!vk_ps4_log_is_open()) {
        vk_ps4_log_open(NULL);
        vk_ps4_log_raw("CreateInstance: log opened");
    }
    VK_PS4_LOG_ENTRY();

    if (!pCreateInfo || !pInstance) {
        vk_ps4_log_raw("CreateInstance: NULL args, FAIL");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkAllocationCallbacks default_alloc = {0};
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &default_alloc;

    VkPs4Instance *inst = vk_ps4_alloc_zero(alloc, sizeof(*inst), 16);
    if (!inst) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    inst->type = VK_PS4_OBJ_INSTANCE;
    if (pAllocator) {
        inst->allocator = *pAllocator;
    }

    /* Pre-allocate the single physical device so handles are stable */
    VkPs4PhysicalDevice *phys = vk_ps4_alloc_zero(alloc, sizeof(*phys), 16);
    if (!phys) {
        vk_ps4_free(alloc, inst);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    phys->type = VK_PS4_OBJ_PHYSICAL_DEVICE;
    phys->instance = inst;

    /* Fill in properties */
    memset(&phys->properties, 0, sizeof(phys->properties));
    phys->properties.apiVersion = VK_PS4_API_VERSION;
    phys->properties.driverVersion = VK_PS4_DRIVER_VERSION;
    phys->properties.vendorID = VK_PS4_CACHE_VENDOR_ID;  /* AMD */
    phys->properties.deviceID = VK_PS4_CACHE_DEVICE_ID;  /* PS4 GPU (Liverpool) */
    phys->properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    strncpy(phys->properties.deviceName, g_device_name,
            sizeof(phys->properties.deviceName) - 1);
    memcpy(phys->properties.pipelineCacheUUID, vk_ps4_pipeline_cache_uuid, VK_UUID_SIZE);

    /* Memory properties: two heaps (Onion + Garlic) */
    memset(&phys->memory_properties, 0, sizeof(phys->memory_properties));
    phys->memory_properties.memoryTypeCount = VK_PS4_MEMORY_TYPE_COUNT;
    phys->memory_properties.memoryTypes[VK_PS4_MEMORY_TYPE_ONION].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    phys->memory_properties.memoryTypes[VK_PS4_MEMORY_TYPE_ONION].heapIndex = 0;
    phys->memory_properties.memoryTypes[VK_PS4_MEMORY_TYPE_GARLIC].propertyFlags =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    phys->memory_properties.memoryTypes[VK_PS4_MEMORY_TYPE_GARLIC].heapIndex = 1;
    phys->memory_properties.memoryHeapCount = 2;
    phys->memory_properties.memoryHeaps[0].size = 2ULL * 1024 * 1024 * 1024;
    phys->memory_properties.memoryHeaps[0].flags = 0;
    phys->memory_properties.memoryHeaps[1].size = 4ULL * 1024 * 1024 * 1024;
    phys->memory_properties.memoryHeaps[1].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

    /* Features — minimal for Vulkan 1.0 */
    memset(&phys->features, 0, sizeof(phys->features));
    phys->features.robustBufferAccess = VK_TRUE;
    phys->features.fullDrawIndexUint32 = VK_TRUE;
    phys->features.imageCubeArray = VK_TRUE;
    phys->features.independentBlend = VK_TRUE;
    phys->features.geometryShader = VK_TRUE;
    phys->features.tessellationShader = VK_TRUE;
    phys->features.sampleRateShading = VK_FALSE; /* only 1x images implemented */
    phys->features.dualSrcBlend = VK_TRUE;
    phys->features.logicOp = VK_TRUE;
    phys->features.multiDrawIndirect = VK_TRUE;
    phys->features.drawIndirectFirstInstance = VK_TRUE;
    phys->features.depthClamp = VK_TRUE;
    phys->features.depthBiasClamp = VK_TRUE;
    phys->features.fillModeNonSolid = VK_TRUE;
    phys->features.depthBounds = VK_TRUE;
    phys->features.wideLines = VK_FALSE;
    phys->features.largePoints = VK_TRUE;
    phys->features.alphaToOne = VK_TRUE;
    phys->features.multiViewport = VK_TRUE;
    phys->features.samplerAnisotropy = VK_TRUE;
    phys->features.textureCompressionBC = VK_TRUE;
    phys->features.occlusionQueryPrecise = VK_TRUE;
    phys->features.pipelineStatisticsQuery = VK_FALSE;
    phys->features.vertexPipelineStoresAndAtomics = VK_TRUE;
    phys->features.fragmentStoresAndAtomics = VK_TRUE;
    phys->features.shaderTessellationAndGeometryPointSize = VK_TRUE;
    phys->features.shaderImageGatherExtended = VK_TRUE;
    phys->features.shaderStorageImageExtendedFormats = VK_TRUE;
    phys->features.shaderStorageImageMultisample = VK_FALSE;
    phys->features.shaderStorageImageReadWithoutFormat = VK_TRUE;
    phys->features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    phys->features.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
    phys->features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    phys->features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    phys->features.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    phys->features.shaderClipDistance = VK_TRUE;
    phys->features.shaderCullDistance = VK_TRUE;
    phys->features.shaderFloat64 = VK_FALSE;
    phys->features.shaderInt64 = VK_TRUE;
    phys->features.shaderInt16 = VK_FALSE;
    phys->features.shaderResourceResidency = VK_FALSE;
    phys->features.shaderResourceMinLod = VK_TRUE;
    phys->features.sparseBinding = VK_FALSE;
    phys->features.sparseResidencyBuffer = VK_FALSE;
    phys->features.sparseResidencyImage2D = VK_FALSE;
    phys->features.sparseResidencyImage3D = VK_FALSE;
    phys->features.sparseResidency2Samples = VK_FALSE;
    phys->features.sparseResidency4Samples = VK_FALSE;
    phys->features.sparseResidency8Samples = VK_FALSE;
    phys->features.sparseResidency16Samples = VK_FALSE;
    phys->features.sparseResidencyAliased = VK_FALSE;
    phys->features.variableMultisampleRate = VK_TRUE;
    phys->features.inheritedQueries = VK_FALSE;

    /* Physical device limits — PS4 Liverpool GPU (GCN 1.0 / GFX7).
     * These values match the PS4 hardware capabilities.  VVL checks
     * many of these against application requests, so they must be
     * non-zero where the hardware supports the feature. */
    VkPhysicalDeviceLimits *l = &phys->properties.limits;
    l->maxImageDimension1D = 16384;
    l->maxImageDimension2D = 16384;
    l->maxImageDimension3D = 2048;
    l->maxImageDimensionCube = 16384;
    l->maxImageArrayLayers = 2048;
    l->maxTexelBufferElements = 134217728;  /* 128M */
    l->maxUniformBufferRange = 65536;       /* 64KB (GCN UBO limit) */
    l->maxStorageBufferRange = 4294967295U; /* 4GB */
    l->maxPushConstantsSize = 256;
    l->maxMemoryAllocationCount = 4096;
    l->maxSamplerAllocationCount = 1048576;  /* 1M */
    l->bufferImageGranularity = 1;           /* no granularity constraint */
    l->sparseAddressSpaceSize = 0;           /* no sparse support */
    l->maxBoundDescriptorSets = 8;
    l->maxPerStageDescriptorSamplers = 16;
    l->maxPerStageDescriptorUniformBuffers = 12;
    l->maxPerStageDescriptorStorageBuffers = 8;
    l->maxPerStageDescriptorSampledImages = 16;
    l->maxPerStageDescriptorStorageImages = 8;
    l->maxPerStageDescriptorInputAttachments = 8;
    l->maxPerStageResources = 32;
    l->maxDescriptorSetSamplers = 96;        /* 16 * 6 stages (VS, TCS, TES, GS, FS, CS) */
    l->maxDescriptorSetUniformBuffers = 72;
    l->maxDescriptorSetUniformBuffersDynamic = 72;
    l->maxDescriptorSetStorageBuffers = 48;
    l->maxDescriptorSetStorageBuffersDynamic = 24;
    l->maxDescriptorSetSampledImages = 96;
    l->maxDescriptorSetStorageImages = 48;
    l->maxDescriptorSetInputAttachments = 48;
    l->maxVertexInputAttributes = 32;
    l->maxVertexInputBindings = 32;
    l->maxVertexInputAttributeOffset = 2047;
    l->maxVertexInputBindingStride = 2048;
    l->maxVertexOutputComponents = 128;
    l->maxTessellationGenerationLevel = 64;
    l->maxTessellationPatchSize = 32;
    l->maxTessellationControlPerVertexInputComponents = 128;
    l->maxTessellationControlPerVertexOutputComponents = 128;
    l->maxTessellationControlPerPatchOutputComponents = 120;
    l->maxTessellationControlTotalOutputComponents = 4096;
    l->maxTessellationEvaluationInputComponents = 128;
    l->maxTessellationEvaluationOutputComponents = 128;
    l->maxGeometryShaderInvocations = 127;
    l->maxGeometryInputComponents = 128;
    l->maxGeometryOutputComponents = 128;
    l->maxGeometryOutputVertices = 1024;
    l->maxGeometryTotalOutputComponents = 4096;
    l->maxFragmentInputComponents = 128;
    l->maxFragmentOutputAttachments = 8;
    l->maxFragmentDualSrcAttachments = 1;
    l->maxFragmentCombinedOutputResources = 8;
    l->maxComputeSharedMemorySize = 32768;   /* 32KB LDS */
    l->maxComputeWorkGroupCount[0] = 65535;
    l->maxComputeWorkGroupCount[1] = 65535;
    l->maxComputeWorkGroupCount[2] = 65535;
    l->maxComputeWorkGroupInvocations = 1024;
    l->maxComputeWorkGroupSize[0] = 1024;
    l->maxComputeWorkGroupSize[1] = 1024;
    l->maxComputeWorkGroupSize[2] = 1024;
    l->subPixelPrecisionBits = 8;
    l->subTexelPrecisionBits = 8;
    l->mipmapPrecisionBits = 8;
    l->maxDrawIndexedIndexValue = 4294967295U;  /* uint32 */
    l->maxDrawIndirectCount = 4294967295U;
    l->maxSamplerLodBias = 16.0f;
    l->maxSamplerAnisotropy = 16.0f;
    l->maxViewports = 16;
    l->maxViewportDimensions[0] = 4096;
    l->maxViewportDimensions[1] = 4096;
    l->viewportBoundsRange[0] = -32768.0f;
    l->viewportBoundsRange[1] = 32767.0f;
    l->viewportSubPixelBits = 8;
    l->minMemoryMapAlignment = 64;            /* Garlic 64-byte alignment */
    l->minTexelBufferOffsetAlignment = 4;     /* 1 texel (R32) */
    l->minUniformBufferOffsetAlignment = 256; /* GCN UBO alignment */
    l->minStorageBufferOffsetAlignment = 4;
    l->minTexelOffset = -64;
    l->maxTexelOffset = 63;
    l->minTexelGatherOffset = -32;
    l->maxTexelGatherOffset = 31;
    l->minInterpolationOffset = -2.0f;
    l->maxInterpolationOffset = 1.0f;
    l->subPixelInterpolationOffsetBits = 8;
    l->maxFramebufferWidth = 4096;
    l->maxFramebufferHeight = 4096;
    l->maxFramebufferLayers = 2048;
    /* Match CreateImage, which rejects samples != 1 until MSAA is implemented. */
    l->framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->maxColorAttachments = 8;
    l->sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l->standardSampleLocations = VK_TRUE;
    l->optimalBufferCopyOffsetAlignment = 1;
    l->optimalBufferCopyRowPitchAlignment = 1;
    l->nonCoherentAtomSize = 64;  /* Garlic cache line */

    /* Missing required limits — spec minimums / feature consistency */
    l->maxSampleMaskWords = 1;
    l->timestampComputeAndGraphics = VK_TRUE;  /* queue reports timestampValidBits=64 */
    l->timestampPeriod = 1.0f;                  /* ns per timestamp tick */
    l->maxClipDistances = 8;                     /* shaderClipDistance = VK_TRUE */
    l->maxCullDistances = 8;                     /* shaderCullDistance = VK_TRUE */
    l->maxCombinedClipAndCullDistances = 8;
    l->discreteQueuePriorities = 2;
    l->pointSizeRange[0] = 1.0f;                 /* largePoints = VK_TRUE */
    l->pointSizeRange[1] = 64.0f;
    l->lineWidthRange[0] = 1.0f;                 /* wideLines = VK_FALSE, so [1,1] */
    l->lineWidthRange[1] = 1.0f;
    l->pointSizeGranularity = 1.0f;
    l->lineWidthGranularity = 1.0f;
    l->strictLines = VK_FALSE;

    inst->physical_device = phys;
    *pInstance = (VkInstance)inst;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator) {
    if (!instance) {
        return;
    }
    VkPs4Instance *inst = (VkPs4Instance *)instance;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &inst->allocator;
    /* Free cached physical device */
    if (inst->physical_device) {
        vk_ps4_free(alloc, inst->physical_device);
        inst->physical_device = NULL;
    }
    vk_ps4_free(alloc, inst);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_EnumeratePhysicalDevices(
    VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices
) {
    if (!instance || !pPhysicalDeviceCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkPs4Instance *inst = (VkPs4Instance *)instance;

    if (!pPhysicalDevices) {
        *pPhysicalDeviceCount = 1;
        return VK_SUCCESS;
    }
    if (*pPhysicalDeviceCount < 1) {
        *pPhysicalDeviceCount = 1;
        return VK_INCOMPLETE;
    }

    /* Return the cached physical device handle (stable across calls) */
    *pPhysicalDevices = (VkPhysicalDevice)inst->physical_device;
    *pPhysicalDeviceCount = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties
) {
    if (!physicalDevice || !pProperties) {
        return;
    }
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;
    *pProperties = phys->properties;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties
) {
    if (!physicalDevice || !pMemoryProperties) {
        return;
    }
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;
    *pMemoryProperties = phys->memory_properties;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueFamilyProperties
) {
    (void)physicalDevice;
    if (!pQueueFamilyPropertyCount) {
        return;
    }
    if (!pQueueFamilyProperties) {
        *pQueueFamilyPropertyCount = VK_PS4_NUM_QUEUE_FAMILIES;
        return;
    }
    if (*pQueueFamilyPropertyCount < VK_PS4_NUM_QUEUE_FAMILIES) {
        /* Report as many as fit */
        if (*pQueueFamilyPropertyCount >= 1) {
            pQueueFamilyProperties[0].queueFlags =
                VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
            pQueueFamilyProperties[0].queueCount = 1;
            pQueueFamilyProperties[0].timestampValidBits = 64;
            pQueueFamilyProperties[0].minImageTransferGranularity = (VkExtent3D){1, 1, 1};
        }
        return;
    }
    /* Family 0: Graphics + Compute + Transfer (the universal queue) */
    pQueueFamilyProperties[0].queueFlags =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[0].queueCount = 1;
    pQueueFamilyProperties[0].timestampValidBits = 64;
    pQueueFamilyProperties[0].minImageTransferGranularity = (VkExtent3D){1, 1, 1};

    /* Family 1: Compute + Transfer (async compute queue).
     * On PS4 (GCN Liverpool), the ACE (Async Compute Engine) can run
     * compute shaders in parallel with graphics.  sceGnmMapComputeQueue
     * maps an ACE pipe to a virtual compute queue.  On host builds the
     * compute queue submits through the same path as graphics. */
    pQueueFamilyProperties[1].queueFlags =
        VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    pQueueFamilyProperties[1].queueCount = 1;
    pQueueFamilyProperties[1].timestampValidBits = 64;
    pQueueFamilyProperties[1].minImageTransferGranularity = (VkExtent3D){1, 1, 1};

    *pQueueFamilyPropertyCount = VK_PS4_NUM_QUEUE_FAMILIES;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures
) {
    if (!physicalDevice || !pFeatures) {
        return;
    }
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;
    *pFeatures = phys->features;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties *pFormatProperties
) {
    (void)physicalDevice;
    if (!pFormatProperties) {
        return;
    }
    *pFormatProperties = vk_ps4_format_properties(format);
}
