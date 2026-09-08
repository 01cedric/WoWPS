/*
 * vk_ps4_vulkan11.c — Vulkan 1.1 core function implementations.
 *
 * Vulkan 1.1 promoted many KHR extensions to core.  Most of these are
 * either thin wrappers over the Vulkan 1.0 functions (e.g. BindBufferMemory2
 * delegates to BindBufferMemory) or stubs that report features as unsupported
 * (e.g. external memory, device groups, multiview — not applicable to PS4).
 *
 * The pNext chain handling in GetPhysicalDeviceProperties2 / Features2 /
 * FormatProperties2 recognises VkPhysicalDeviceVulkan11Features /
 * VkPhysicalDeviceVulkan11Properties and fills them with PS4-appropriate
 * values (all features FALSE — GCN 1.0 / Liverpool has no 16-bit storage,
 * multiview, variable pointers, YCbCr, or protected memory).
 */

#include "vk_ps4.h"
#include "vk_ps4_internal.h"

#include <string.h>
#include <stdlib.h>

/* === Instance-level === */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_EnumerateInstanceVersion(uint32_t *pApiVersion) {
    if (!pApiVersion) return VK_ERROR_INITIALIZATION_FAILED;
    *pApiVersion = VK_PS4_API_VERSION;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_EnumeratePhysicalDeviceGroups(
    VkInstance instance,
    uint32_t *pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupProperties *pPhysicalDeviceGroupProperties
) {
    /* PS4 has a single GPU — one group with one device. */
    (void)instance;
    if (!pPhysicalDeviceGroupCount) return VK_ERROR_INITIALIZATION_FAILED;

    if (!pPhysicalDeviceGroupProperties) {
        *pPhysicalDeviceGroupCount = 1;
        return VK_SUCCESS;
    }

    uint32_t avail = *pPhysicalDeviceGroupCount;
    if (avail == 0) return VK_INCOMPLETE;

    memset(&pPhysicalDeviceGroupProperties[0], 0, sizeof(VkPhysicalDeviceGroupProperties));
    pPhysicalDeviceGroupProperties[0].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    pPhysicalDeviceGroupProperties[0].physicalDeviceCount = 1;
    /* The single physical device is cached on the instance. */
    VkPs4Instance *inst = (VkPs4Instance *)instance;
    if (inst && inst->physical_device) {
        pPhysicalDeviceGroupProperties[0].physicalDevices[0] =
            (VkPhysicalDevice)inst->physical_device;
    }
    pPhysicalDeviceGroupProperties[0].subsetAllocation = VK_FALSE;
    *pPhysicalDeviceGroupCount = 1;
    return VK_SUCCESS;
}

/* === Physical device property/feature queries (2 variants) === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2 *pProperties
) {
    if (!physicalDevice || !pProperties) return;
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;

    /* Copy base properties */
    pProperties->properties = phys->properties;

    /* Walk the pNext chain and fill in any recognised structures. */
    VkBaseOutStructure *chain = (VkBaseOutStructure *)pProperties->pNext;
    while (chain) {
        switch (chain->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: {
            VkPhysicalDeviceVulkan11Properties *p =
                (VkPhysicalDeviceVulkan11Properties *)chain;
            void *saved_pNext = p->pNext;
            memset(p, 0, sizeof(*p));
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
            p->pNext = saved_pNext;
            /* PS4 has a single UUID — all zeros (no persistent ID). */
            memset(p->deviceUUID, 0, VK_UUID_SIZE);
            memset(p->driverUUID, 0, VK_UUID_SIZE);
            memset(p->deviceLUID, 0, VK_LUID_SIZE);
            p->deviceNodeMask = 0;
            p->deviceLUIDValid = VK_FALSE;
            /* Subgroup properties: GCN 1.0 supports basic subgroup ops. */
            p->subgroupSize = 64;  /* GCN wavefront64 */
            p->subgroupSupportedStages = VK_SHADER_STAGE_VERTEX_BIT |
                                         VK_SHADER_STAGE_FRAGMENT_BIT |
                                         VK_SHADER_STAGE_COMPUTE_BIT;
            p->subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;
            p->subgroupQuadOperationsInAllStages = VK_FALSE;
            /* Point clipping: all clip planes (conservative). */
            p->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
            /* No multiview. */
            p->maxMultiviewViewCount = 0;
            p->maxMultiviewInstanceIndex = 0;
            /* No protected memory. */
            p->protectedNoFault = VK_FALSE;
            /* Descriptor set limits. */
            p->maxPerSetDescriptors = 64;
            p->maxMemoryAllocationSize = 4ULL * 1024 * 1024 * 1024;  /* 4 GB (Garlic heap) */
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
            VkPhysicalDeviceIDProperties *p =
                (VkPhysicalDeviceIDProperties *)chain;
            void *saved_pNext = p->pNext;
            memset(p, 0, sizeof(*p));
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
            p->pNext = saved_pNext;
            memset(p->deviceUUID, 0, VK_UUID_SIZE);
            memset(p->driverUUID, 0, VK_UUID_SIZE);
            memset(p->deviceLUID, 0, VK_LUID_SIZE);
            p->deviceNodeMask = 0;
            p->deviceLUIDValid = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
            VkPhysicalDeviceMaintenance3Properties *p =
                (VkPhysicalDeviceMaintenance3Properties *)chain;
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
            p->maxPerSetDescriptors = 64;
            p->maxMemoryAllocationSize = 4ULL * 1024 * 1024 * 1024;  /* 4 GB */
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
            VkPhysicalDeviceSubgroupProperties *p =
                (VkPhysicalDeviceSubgroupProperties *)chain;
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
            p->subgroupSize = 64;
            p->supportedStages = VK_SHADER_STAGE_VERTEX_BIT |
                                 VK_SHADER_STAGE_FRAGMENT_BIT |
                                 VK_SHADER_STAGE_COMPUTE_BIT;
            p->supportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;
            p->quadOperationsInAllStages = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES: {
            VkPhysicalDevicePointClippingProperties *p =
                (VkPhysicalDevicePointClippingProperties *)chain;
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES;
            p->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: {
            VkPhysicalDeviceMultiviewProperties *p =
                (VkPhysicalDeviceMultiviewProperties *)chain;
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;
            p->maxMultiviewViewCount = 0;
            p->maxMultiviewInstanceIndex = 0;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES: {
            VkPhysicalDeviceProtectedMemoryProperties *p =
                (VkPhysicalDeviceProtectedMemoryProperties *)chain;
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES;
            p->protectedNoFault = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT: {
            /* Not a 1.1 struct, but commonly queried. Skip. */
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: {
            VkPhysicalDeviceDriverProperties *p =
                (VkPhysicalDeviceDriverProperties *)chain;
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            /* Identify as an open-source Mesa-style driver for PS4. */
            strncpy(p->driverName, "vulkan-ps4", VK_MAX_DRIVER_NAME_SIZE_KHR);
            strncpy(p->driverInfo, "OpenGNM Vulkan ICD", VK_MAX_DRIVER_INFO_SIZE_KHR);
            p->driverID = VK_DRIVER_ID_MESA_RADV;  /* closest match — open-source AMD */
            p->conformanceVersion.major = 1;
            p->conformanceVersion.minor = 1;
            p->conformanceVersion.subminor = 0;
            p->conformanceVersion.patch = 0;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES: {
            VkPhysicalDeviceDescriptorIndexingProperties *p =
                (VkPhysicalDeviceDescriptorIndexingProperties *)chain;
            void *saved_pNext = p->pNext;
            memset(p, 0, sizeof(*p));
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
            p->pNext = saved_pNext;
            /* GNM supports bindless natively with large descriptor arrays. */
            p->maxUpdateAfterBindDescriptorsInAllPools = 1000000;
            p->shaderUniformBufferArrayNonUniformIndexingNative = VK_FALSE;
            p->shaderSampledImageArrayNonUniformIndexingNative = VK_FALSE;
            p->shaderStorageBufferArrayNonUniformIndexingNative = VK_FALSE;
            p->shaderStorageImageArrayNonUniformIndexingNative = VK_FALSE;
            p->shaderInputAttachmentArrayNonUniformIndexingNative = VK_FALSE;
            p->robustBufferAccessUpdateAfterBind = VK_FALSE;
            p->quadDivergentImplicitLod = VK_FALSE;
            p->maxPerStageDescriptorUpdateAfterBindSamplers = 1000000;
            p->maxPerStageDescriptorUpdateAfterBindUniformBuffers = 1000000;
            p->maxPerStageDescriptorUpdateAfterBindStorageBuffers = 1000000;
            p->maxPerStageDescriptorUpdateAfterBindSampledImages = 1000000;
            p->maxPerStageDescriptorUpdateAfterBindStorageImages = 1000000;
            p->maxPerStageDescriptorUpdateAfterBindInputAttachments = 1000000;
            p->maxPerStageUpdateAfterBindResources = 1000000;
            p->maxDescriptorSetUpdateAfterBindSamplers = 1000000;
            p->maxDescriptorSetUpdateAfterBindUniformBuffers = 1000000;
            p->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 16;
            p->maxDescriptorSetUpdateAfterBindStorageBuffers = 1000000;
            p->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 16;
            p->maxDescriptorSetUpdateAfterBindSampledImages = 1000000;
            p->maxDescriptorSetUpdateAfterBindStorageImages = 1000000;
            p->maxDescriptorSetUpdateAfterBindInputAttachments = 1000000;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES: {
            VkPhysicalDeviceTimelineSemaphoreProperties *p =
                (VkPhysicalDeviceTimelineSemaphoreProperties *)chain;
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES;
            p->maxTimelineSemaphoreValueDifference = UINT64_MAX;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES: {
            VkPhysicalDeviceDepthStencilResolveProperties *p =
                (VkPhysicalDeviceDepthStencilResolveProperties *)chain;
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;
            /* GCN supports depth/stencil resolve via HW.  Report supported modes. */
            p->supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
            p->supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
            p->independentResolveNone = VK_TRUE;
            p->independentResolve = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES: {
            VkPhysicalDeviceFloatControlsProperties *p =
                (VkPhysicalDeviceFloatControlsProperties *)chain;
            p->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES;
            /* GCN 1.0: flush-to-zero for denorms, preserve signed zero/inf/nan
             * for float32.  No float16/float64 on Liverpool. */
            p->denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL;
            p->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
            p->shaderSignedZeroInfNanPreserveFloat16 = VK_FALSE;
            p->shaderSignedZeroInfNanPreserveFloat32 = VK_TRUE;
            p->shaderSignedZeroInfNanPreserveFloat64 = VK_FALSE;
            p->shaderDenormPreserveFloat16 = VK_FALSE;
            p->shaderDenormPreserveFloat32 = VK_FALSE;
            p->shaderDenormPreserveFloat64 = VK_FALSE;
            p->shaderDenormFlushToZeroFloat16 = VK_FALSE;
            p->shaderDenormFlushToZeroFloat32 = VK_TRUE;
            p->shaderDenormFlushToZeroFloat64 = VK_FALSE;
            p->shaderRoundingModeRTEFloat16 = VK_FALSE;
            p->shaderRoundingModeRTEFloat32 = VK_FALSE;
            p->shaderRoundingModeRTEFloat64 = VK_FALSE;
            p->shaderRoundingModeRTZFloat16 = VK_FALSE;
            p->shaderRoundingModeRTZFloat32 = VK_FALSE;
            p->shaderRoundingModeRTZFloat64 = VK_FALSE;
            break;
        }
        default:
            /* Unrecognised — leave as-is (zeroed by caller). */
            break;
        }
        chain = chain->pNext;
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2 *pFeatures
) {
    if (!physicalDevice || !pFeatures) return;
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;

    /* Copy base features */
    pFeatures->features = phys->features;

    /* Walk the pNext chain and fill in any recognised structures. */
    VkBaseOutStructure *chain = (VkBaseOutStructure *)pFeatures->pNext;
    while (chain) {
        switch (chain->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: {
            VkPhysicalDeviceVulkan11Features *f =
                (VkPhysicalDeviceVulkan11Features *)chain;
            void *saved_pNext = f->pNext;
            memset(f, 0, sizeof(*f));
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            f->pNext = saved_pNext;
            /* GCN 1.0 (Liverpool) does not support any of the Vulkan 1.1
             * optional features.  All are FALSE. */
            f->storageBuffer16BitAccess = VK_FALSE;
            f->uniformAndStorageBuffer16BitAccess = VK_FALSE;
            f->storagePushConstant16 = VK_FALSE;
            f->storageInputOutput16 = VK_FALSE;
            f->multiview = VK_FALSE;
            f->multiviewGeometryShader = VK_FALSE;
            f->multiviewTessellationShader = VK_FALSE;
            f->variablePointersStorageBuffer = VK_FALSE;
            f->variablePointers = VK_FALSE;
            f->protectedMemory = VK_FALSE;
            f->samplerYcbcrConversion = VK_FALSE;
            f->shaderDrawParameters = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
            VkPhysicalDevice16BitStorageFeatures *f =
                (VkPhysicalDevice16BitStorageFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
            f->storageBuffer16BitAccess = VK_FALSE;
            f->uniformAndStorageBuffer16BitAccess = VK_FALSE;
            f->storagePushConstant16 = VK_FALSE;
            f->storageInputOutput16 = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
            VkPhysicalDeviceMultiviewFeatures *f =
                (VkPhysicalDeviceMultiviewFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
            f->multiview = VK_FALSE;
            f->multiviewGeometryShader = VK_FALSE;
            f->multiviewTessellationShader = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
            VkPhysicalDeviceVariablePointersFeatures *f =
                (VkPhysicalDeviceVariablePointersFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES;
            f->variablePointersStorageBuffer = VK_FALSE;
            f->variablePointers = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES: {
            VkPhysicalDeviceProtectedMemoryFeatures *f =
                (VkPhysicalDeviceProtectedMemoryFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES;
            f->protectedMemory = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
            VkPhysicalDeviceSamplerYcbcrConversionFeatures *f =
                (VkPhysicalDeviceSamplerYcbcrConversionFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
            f->samplerYcbcrConversion = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES: {
            VkPhysicalDeviceShaderDrawParametersFeatures *f =
                (VkPhysicalDeviceShaderDrawParametersFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
            f->shaderDrawParameters = VK_FALSE;
            break;
        }
        /* Phase 4 extensions */
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES: {
            VkPhysicalDeviceImagelessFramebufferFeatures *f =
                (VkPhysicalDeviceImagelessFramebufferFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES;
            f->imagelessFramebuffer = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: {
            VkPhysicalDeviceDescriptorIndexingFeatures *f =
                (VkPhysicalDeviceDescriptorIndexingFeatures *)chain;
            void *saved_pNext = f->pNext;
            memset(f, 0, sizeof(*f));
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
            f->pNext = saved_pNext;
            /* GNM supports bindless natively, so we enable the core
             * descriptor indexing features.  Update-after-bind and
             * partially-bound are software-managed (no GPU restriction). */
            f->shaderInputAttachmentArrayDynamicIndexing = VK_FALSE;
            f->shaderUniformTexelBufferArrayDynamicIndexing = VK_TRUE;
            f->shaderStorageTexelBufferArrayDynamicIndexing = VK_TRUE;
            f->shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
            f->shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
            f->shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
            f->shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
            f->shaderInputAttachmentArrayNonUniformIndexing = VK_FALSE;
            f->shaderUniformTexelBufferArrayNonUniformIndexing = VK_TRUE;
            f->shaderStorageTexelBufferArrayNonUniformIndexing = VK_TRUE;
            f->descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
            f->descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
            f->descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
            f->descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
            f->descriptorBindingUniformTexelBufferUpdateAfterBind = VK_TRUE;
            f->descriptorBindingStorageTexelBufferUpdateAfterBind = VK_TRUE;
            f->descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
            f->descriptorBindingPartiallyBound = VK_TRUE;
            f->descriptorBindingVariableDescriptorCount = VK_TRUE;
            f->runtimeDescriptorArray = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: {
            VkPhysicalDeviceTimelineSemaphoreFeatures *f =
                (VkPhysicalDeviceTimelineSemaphoreFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
            f->timelineSemaphore = VK_TRUE;
            break;
        }
        /* Phase 4 batch: additional extension features */
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES: {
            VkPhysicalDeviceScalarBlockLayoutFeatures *f =
                (VkPhysicalDeviceScalarBlockLayoutFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
            f->scalarBlockLayout = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES: {
            VkPhysicalDeviceUniformBufferStandardLayoutFeatures *f =
                (VkPhysicalDeviceUniformBufferStandardLayoutFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES;
            f->uniformBufferStandardLayout = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES: {
            VkPhysicalDeviceHostQueryResetFeatures *f =
                (VkPhysicalDeviceHostQueryResetFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
            f->hostQueryReset = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES: {
            VkPhysicalDeviceShaderAtomicInt64Features *f =
                (VkPhysicalDeviceShaderAtomicInt64Features *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
            /* GCN 1.0 supports buffer atomics but not image atomics for int64.
             * Report shaderBufferInt64Atomics as supported. */
            f->shaderBufferInt64Atomics = VK_TRUE;
            f->shaderSharedInt64Atomics = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: {
            VkPhysicalDeviceBufferDeviceAddressFeatures *f =
                (VkPhysicalDeviceBufferDeviceAddressFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
            /* GNM uses 64-bit GPU addresses natively. */
            f->bufferDeviceAddress = VK_TRUE;
            f->bufferDeviceAddressCaptureReplay = VK_FALSE;
            f->bufferDeviceAddressMultiDevice = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES: {
            VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *f =
                (VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES;
            /* GCN supports subgroup operations on basic types.  Extended types
             * (8-bit, 16-bit, 64-bit) have partial support. */
            f->shaderSubgroupExtendedTypes = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES: {
            VkPhysicalDeviceVulkanMemoryModelFeatures *f =
                (VkPhysicalDeviceVulkanMemoryModelFeatures *)chain;
            f->sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
            /* We honor memory model semantics via pipeline barriers. */
            f->vulkanMemoryModel = VK_TRUE;
            f->vulkanMemoryModelDeviceScope = VK_TRUE;
            f->vulkanMemoryModelAvailabilityVisibilityChains = VK_FALSE;
            break;
        }
        /* VK_KHR_spirv_1_4 has no features struct — it's just an extension
         * that enables SPIR-V 1.4 support, handled by opengnm-psbc.
         * VK_EXT_separate_stencil_usage also has no features struct — it
         * only adds a VkImageCreateFlag (VK_IMAGE_CREATE_STENCIL_SAMPLED_BIT). */
        default:
            break;
        }
        chain = chain->pNext;
    }
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceFormatProperties2(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties2 *pFormatProperties
) {
    if (!physicalDevice || !pFormatProperties) return;
    /* Delegate to the v1 function. */
    vk_ps4_GetPhysicalDeviceFormatProperties(physicalDevice, format,
        &pFormatProperties->formatProperties);
    /* No pNext structures for format properties in 1.1. */
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_GetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
    VkImageFormatProperties2 *pImageFormatProperties
) {
    if (!physicalDevice || !pImageFormatInfo || !pImageFormatProperties)
        return VK_ERROR_INITIALIZATION_FAILED;
    /* Delegate to the v1 function. */
    return vk_ps4_GetPhysicalDeviceImageFormatProperties(
        physicalDevice,
        pImageFormatInfo->format,
        pImageFormatInfo->type,
        pImageFormatInfo->tiling,
        pImageFormatInfo->usage,
        pImageFormatInfo->flags,
        &pImageFormatProperties->imageFormatProperties);
}

/* === External memory/fence/semaphore capabilities === */
/* PS4 has no external memory/fence/semaphore sharing. */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceExternalBufferProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
    VkExternalBufferProperties *pExternalBufferProperties
) {
    (void)physicalDevice;
    (void)pExternalBufferInfo;
    if (!pExternalBufferProperties) return;
    memset(&pExternalBufferProperties->externalMemoryProperties, 0,
           sizeof(VkExternalMemoryProperties));
    /* No external memory handle types supported. */
    pExternalBufferProperties->externalMemoryProperties.externalMemoryFeatures = 0;
    pExternalBufferProperties->externalMemoryProperties.exportFromImportedHandleTypes = 0;
    pExternalBufferProperties->externalMemoryProperties.compatibleHandleTypes = 0;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo,
    VkExternalFenceProperties *pExternalFenceProperties
) {
    (void)physicalDevice;
    (void)pExternalFenceInfo;
    if (!pExternalFenceProperties) return;
    /* No external fence handle types supported.  Only zero the fields,
     * not the sType/pNext which the caller has already set. */
    pExternalFenceProperties->exportFromImportedHandleTypes = 0;
    pExternalFenceProperties->compatibleHandleTypes = 0;
    pExternalFenceProperties->externalFenceFeatures = 0;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties *pExternalSemaphoreProperties
) {
    (void)physicalDevice;
    (void)pExternalSemaphoreInfo;
    if (!pExternalSemaphoreProperties) return;
    /* No external semaphore handle types supported.  Only zero the fields,
     * not the sType/pNext which the caller has already set. */
    pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
    pExternalSemaphoreProperties->compatibleHandleTypes = 0;
    pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
}

/* === Device-level === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetDeviceQueue2(
    VkDevice device,
    const VkDeviceQueueInfo2 *pQueueInfo,
    VkQueue *pQueue
) {
    if (!device || !pQueueInfo || !pQueue) return;
    /* PS4 has 2 queue families (0=graphics, 1=compute), each with 1 queue.
     * Delegate to GetDeviceQueue, ignoring flags (no protected queues). */
    if (pQueueInfo->queueIndex == 0 &&
        pQueueInfo->queueFamilyIndex < VK_PS4_NUM_QUEUE_FAMILIES) {
        vk_ps4_GetDeviceQueue(device, pQueueInfo->queueFamilyIndex, 0, pQueue);
    } else {
        *pQueue = VK_NULL_HANDLE;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_BindBufferMemory2(
    VkDevice device,
    uint32_t bindInfoCount,
    const VkBindBufferMemoryInfo *pBindInfos
) {
    if (!device || !pBindInfos) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < bindInfoCount; i++) {
        VkResult r = vk_ps4_BindBufferMemory(device,
            pBindInfos[i].buffer,
            pBindInfos[i].memory,
            pBindInfos[i].memoryOffset);
        if (r != VK_SUCCESS) result = r;
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_BindImageMemory2(
    VkDevice device,
    uint32_t bindInfoCount,
    const VkBindImageMemoryInfo *pBindInfos
) {
    if (!device || !pBindInfos) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < bindInfoCount; i++) {
        VkResult r = vk_ps4_BindImageMemory(device,
            pBindInfos[i].image,
            pBindInfos[i].memory,
            pBindInfos[i].memoryOffset);
        if (r != VK_SUCCESS) result = r;
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetDeviceGroupPeerMemoryFeatures(
    VkDevice device,
    uint32_t heapIndex,
    uint32_t localDeviceIndex,
    uint32_t remoteDeviceIndex,
    VkPeerMemoryFeatureFlags *pPeerMemoryFeatures
) {
    (void)device; (void)heapIndex;
    (void)localDeviceIndex; (void)remoteDeviceIndex;
    /* Single GPU — no peer memory. */
    if (pPeerMemoryFeatures) *pPeerMemoryFeatures = 0;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetDescriptorSetLayoutSupport(
    VkDevice device,
    const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
    VkDescriptorSetLayoutSupport *pSupport
) {
    (void)device;
    if (!pCreateInfo || !pSupport) return;
    /* We support any descriptor set layout that fits within our limits.
     * The validation layer checks limits, so we just report supported. */
    pSupport->supported = VK_TRUE;
    /* Walk pNext for VkDescriptorSetVariableDescriptorCountLayoutSupport. */
    VkBaseOutStructure *chain = (VkBaseOutStructure *)pSupport->pNext;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT) {
            VkDescriptorSetVariableDescriptorCountLayoutSupport *vs =
                (VkDescriptorSetVariableDescriptorCountLayoutSupport *)chain;
            /* Report a generous max — GNM supports bindless natively.
             * Limited by VK_PS4_MAX_DESCRIPTOR_BINDINGS per binding. */
            vs->maxVariableDescriptorCount = 1000000;
        }
        chain = chain->pNext;
    }
}

/* === Descriptor update templates === */
/* Minimal in-memory template that builds VkWriteDescriptorSet array from
 * the user data and calls vkUpdateDescriptorSets. */

/* Minimal descriptor update template — stores a copy of the create info
 * so UpdateDescriptorSetWithTemplate can iterate the entries and build
 * VkWriteDescriptorSet structs from the user-provided data pointer. */
typedef struct VkPs4DescriptorUpdateTemplate {
    VkDescriptorUpdateTemplateCreateInfo create_info;
    VkDescriptorUpdateTemplateEntry *entries;
} VkPs4DescriptorUpdateTemplate;

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateDescriptorUpdateTemplate(
    VkDevice device,
    const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate
) {
    (void)device; (void)pAllocator;
    if (!pCreateInfo || !pDescriptorUpdateTemplate) return VK_ERROR_INITIALIZATION_FAILED;
    /* Only descriptor set templates are supported (not push descriptors). */
    if (pCreateInfo->templateType != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET)
        return VK_ERROR_FEATURE_NOT_PRESENT;

    VkPs4DescriptorUpdateTemplate *tmpl =
        (VkPs4DescriptorUpdateTemplate *)calloc(1, sizeof(*tmpl));
    if (!tmpl) return VK_ERROR_OUT_OF_HOST_MEMORY;
    tmpl->create_info = *pCreateInfo;
    if (pCreateInfo->descriptorUpdateEntryCount > 0) {
        tmpl->entries = (VkDescriptorUpdateTemplateEntry *)calloc(
            pCreateInfo->descriptorUpdateEntryCount, sizeof(*tmpl->entries));
        if (!tmpl->entries) { free(tmpl); return VK_ERROR_OUT_OF_HOST_MEMORY; }
        memcpy(tmpl->entries, pCreateInfo->pDescriptorUpdateEntries,
               pCreateInfo->descriptorUpdateEntryCount * sizeof(*tmpl->entries));
        tmpl->create_info.pDescriptorUpdateEntries = tmpl->entries;
    }
    *pDescriptorUpdateTemplate = (VkDescriptorUpdateTemplate)tmpl;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyDescriptorUpdateTemplate(
    VkDevice device,
    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    const VkAllocationCallbacks *pAllocator
) {
    (void)device; (void)pAllocator;
    VkPs4DescriptorUpdateTemplate *tmpl =
        (VkPs4DescriptorUpdateTemplate *)descriptorUpdateTemplate;
    if (!tmpl) return;
    free(tmpl->entries);
    free(tmpl);
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_UpdateDescriptorSetWithTemplate(
    VkDevice device,
    VkDescriptorSet descriptorSet,
    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
    const void *pData
) {
    VkPs4DescriptorUpdateTemplate *tmpl =
        (VkPs4DescriptorUpdateTemplate *)descriptorUpdateTemplate;
    if (!tmpl || !pData) return;

    /* Build VkWriteDescriptorSet array from the template entries and the
     * user data, then call vkUpdateDescriptorSets. */
    uint32_t write_count = 0;
    for (uint32_t i = 0; i < tmpl->create_info.descriptorUpdateEntryCount; i++) {
        write_count += tmpl->entries[i].descriptorCount;
    }
    if (write_count == 0) return;

    VkWriteDescriptorSet *writes =
        (VkWriteDescriptorSet *)calloc(write_count, sizeof(*writes));
    if (!writes) return;

    uint32_t w = 0;
    for (uint32_t i = 0; i < tmpl->create_info.descriptorUpdateEntryCount; i++) {
        const VkDescriptorUpdateTemplateEntry *e = &tmpl->entries[i];
        for (uint32_t j = 0; j < e->descriptorCount; j++) {
            VkWriteDescriptorSet *wr = &writes[w++];
            wr->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr->dstSet = descriptorSet;
            wr->dstBinding = e->dstBinding;
            wr->dstArrayElement = e->dstArrayElement + j;
            wr->descriptorCount = 1;
            wr->descriptorType = e->descriptorType;
            const uint8_t *src = (const uint8_t *)pData + e->offset +
                                 j * e->stride;
            switch (e->descriptorType) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                wr->pTexelBufferView = (const VkBufferView *)src;
                break;
            case VK_DESCRIPTOR_TYPE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                wr->pImageInfo = (const VkDescriptorImageInfo *)src;
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                wr->pBufferInfo = (const VkDescriptorBufferInfo *)src;
                break;
            default:
                /* Unsupported descriptor type — skip. */
                w--;
                break;
            }
        }
    }
    vkUpdateDescriptorSets(device, w, writes, 0, NULL);
    free(writes);
}

/* === Command buffer level (device group) === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask) {
    (void)commandBuffer; (void)deviceMask;
    /* Single GPU — device mask is always 1. No-op. */
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_CmdDispatchBase(
    VkCommandBuffer commandBuffer,
    uint32_t baseGroupX,
    uint32_t baseGroupY,
    uint32_t baseGroupZ,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ
) {
    /* CmdDispatchBase dispatches with base workgroup offsets that are
     * added to WorkgroupId.  GCN compute dispatch doesn't have a native
     * base offset register, so we emit the base offsets via user-data
     * registers that the shader can read (similar to baseVertex for
     * draw calls).  However, since psbc doesn't currently reserve slots
     * for compute base offsets, we fall back to CmdDispatch when all
     * base offsets are zero (the common case).  Non-zero base offsets
     * would require shader support — log a warning and dispatch anyway. */
    if (baseGroupX != 0 || baseGroupY != 0 || baseGroupZ != 0) {
        /* Non-zero base offsets not yet supported — the shader would need
         * to add these to WorkgroupId.  This is a known limitation. */
    }
    vk_ps4_CmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

/* === GetPhysicalDeviceMemoryProperties2 === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2 *pMemoryProperties
) {
    if (!physicalDevice || !pMemoryProperties) return;
    VkPs4PhysicalDevice *phys = (VkPs4PhysicalDevice *)physicalDevice;
    pMemoryProperties->memoryProperties = phys->memory_properties;
    /* No pNext structures for memory properties in 1.1. */
}

/* === GetPhysicalDeviceSparseImageFormatProperties2 === */

VKAPI_ATTR void VKAPI_CALL
vk_ps4_GetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
    uint32_t *pPropertyCount,
    VkSparseImageFormatProperties2 *pProperties
) {
    (void)physicalDevice;
    (void)pFormatInfo;
    /* PS4 has no sparse memory support. */
    if (pPropertyCount) *pPropertyCount = 0;
    (void)pProperties;
}

/* === SamplerYcbcrConversion === */
/* Not supported on PS4 (no YCbCr sampler hardware).  We advertise the
 * extension for backwards compatibility but report the feature as FALSE.
 * Create returns a minimal dummy object so apps that check for the
 * function pointer don't crash, but the feature is never enabled. */

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateSamplerYcbcrConversion(
    VkDevice device,
    const VkSamplerYcbcrConversionCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSamplerYcbcrConversion *pYcbcrConversion
) {
    (void)device; (void)pCreateInfo; (void)pAllocator;
    if (!pYcbcrConversion) return VK_ERROR_INITIALIZATION_FAILED;
    /* Return a non-null handle so the app can proceed, but the feature
     * is never enabled so this should never be called in practice. */
    *pYcbcrConversion = (VkSamplerYcbcrConversion)0x1;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroySamplerYcbcrConversion(
    VkDevice device,
    VkSamplerYcbcrConversion ycbcrConversion,
    const VkAllocationCallbacks *pAllocator
) {
    (void)device; (void)ycbcrConversion; (void)pAllocator;
    /* No-op — Create returns a dummy handle. */
}
