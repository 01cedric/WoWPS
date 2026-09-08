/*
 * vk_ps4_shader.c — VkShaderModule implementation via libpsbc.
 *
 * vkCreateShaderModule compiles SPIR-V → GCN binary using libpsbc,
 * then extracts shader metadata (stage registers, input usage slots)
 * from the GnmShaderFileHeader.
 */

#include "vk_ps4_internal.h"

#include <string.h>
#include <stdio.h>

#ifdef VK_PS4_HAVE_PSBC
#include "psbc_compile.h"
#endif

#ifdef VK_PS4_HAVE_SPIRV_TOOLS
#include <spirv-tools/libspirv.h>
#endif

VKAPI_ATTR VkResult VKAPI_CALL
vk_ps4_CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule) {
    VK_PS4_LOG_ENTRY();
    if (!device || !pCreateInfo || !pShaderModule) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pCreateInfo->sType != VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPs4Device *dev = (VkPs4Device *)device;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;

    /* Basic SPIR-V sanity checks:
     * - codeSize must be a multiple of 4 (SPIR-V is a stream of uint32_t)
     * - minimum size is the 5-word header (20 bytes)
     * - magic number must be 0x07230203 */
    size_t spirv_size = pCreateInfo->codeSize;
    if (spirv_size == 0 || !pCreateInfo->pCode) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (spirv_size < 20 || (spirv_size % 4) != 0) {
        return VK_ERROR_INVALID_SHADER_NV;
    }
    const uint32_t *spirv_words = (const uint32_t *)pCreateInfo->pCode;
    if (spirv_words[0] != 0x07230203) {
        /* Not a valid SPIR-V magic number */
        return VK_ERROR_INVALID_SHADER_NV;
    }

#ifdef VK_PS4_HAVE_SPIRV_TOOLS
    /* Validate SPIR-V using SPIRV-Tools before storing it.
     * This catches malformed shaders early — before pipeline creation
     * triggers the expensive psbc compile path.  We use Vulkan 1.1
     * target env since the ICD advertises Vulkan 1.1. */
    {
        spv_context ctx = spvContextCreate(SPV_ENV_VULKAN_1_1);
        if (ctx) {
            spv_diagnostic diag = NULL;
            spv_result_t result = spvValidateBinary(
                ctx,
                spirv_words,
                spirv_size / sizeof(uint32_t),
                &diag
            );
            if (result != SPV_SUCCESS) {
                if (diag) {
                    fprintf(stderr, "vk_ps4: SPIR-V validation failed: %s\n",
                            diag->error ? diag->error : "(no message)");
                    vk_ps4_log("CreateShaderModule: SPIR-V validation FAILED: %s",
                               diag->error ? diag->error : "(no message)");
                    spvDiagnosticDestroy(diag);
                } else {
                    fprintf(stderr, "vk_ps4: SPIR-V validation failed (code %d)\n", result);
                    vk_ps4_log("CreateShaderModule: SPIR-V validation FAILED (code %d)", result);
                }
                spvContextDestroy(ctx);
                return VK_ERROR_INVALID_SHADER_NV;
            }
            if (diag) spvDiagnosticDestroy(diag);
            spvContextDestroy(ctx);
        }
    }
#endif

    VkPs4ShaderModule *mod = vk_ps4_alloc_zero(alloc, sizeof(*mod), 16);
    if (!mod) return VK_ERROR_OUT_OF_HOST_MEMORY;
    mod->type = VK_PS4_OBJ_SHADER_MODULE;
    mod->device = dev;
    mod->binary = NULL;
    mod->binary_size = 0;
    mod->has_metadata = false;

#ifdef VK_PS4_HAVE_PSBC
    /* Compile SPIR-V → GCN binary using libpsbc.
     * We don't know the stage from the module alone — SPIR-V doesn't encode
     * the stage in the module create info. The stage is determined when
     * creating the pipeline. So we store the SPIR-V and compile at pipeline
     * creation time.
     *
     * However, for the MVP we can try to detect the stage from SPIR-V
     * capabilities. For now, just store the SPIR-V and defer compilation. */

    /* Store a copy of the SPIR-V for later compilation.
     * spirv_size and pCode were already validated above. */
    void *spirv_copy = vk_ps4_alloc(alloc, spirv_size, 4);
    if (!spirv_copy) {
        vk_ps4_free(alloc, mod);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(spirv_copy, pCreateInfo->pCode, spirv_size);

    /* Store SPIR-V as the "binary" for now — pipeline creation will compile it */
    mod->binary = spirv_copy;
    mod->binary_size = spirv_size;
#else
    /* No libpsbc — store SPIR-V as-is (stub mode).
     * spirv_size and pCode were already validated above. */
    void *spirv_copy = vk_ps4_alloc(alloc, spirv_size, 4);
    if (!spirv_copy) {
        vk_ps4_free(alloc, mod);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(spirv_copy, pCreateInfo->pCode, spirv_size);
    mod->binary = spirv_copy;
    mod->binary_size = spirv_size;
#endif

    *pShaderModule = (VkShaderModule)mod;
    vk_ps4_log("CreateShaderModule: OK (size=%zu)", spirv_size);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_ps4_DestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator) {
    if (!device || !shaderModule) return;
    VkPs4Device *dev = (VkPs4Device *)device;
    VkPs4ShaderModule *mod = (VkPs4ShaderModule *)shaderModule;
    const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &dev->allocator;
    if (mod->binary) {
        vk_ps4_free(alloc, mod->binary);
    }
    vk_ps4_free(alloc, mod);
}

/* Helper: compile a shader module for a specific stage using libpsbc.
 * Called from pipeline creation. Returns the compiled binary and metadata. */
VkResult vk_ps4_compile_shader_module(VkPs4ShaderModule *mod, VkShaderStageFlagBits stage,
                                      const VkPs4PipelineLayout *layout,
                                      const VkAllocationCallbacks *alloc,
                                      void **out_binary, size_t *out_binary_size,
                                      GnmShaderMetadata *out_metadata) {
    if (!mod || !mod->binary || mod->binary_size == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

#ifdef VK_PS4_HAVE_PSBC
    /* libpsbc is initialized at device creation time (vk_ps4_CreateDevice).
     * No per-compile init/shutdown needed — psbc_compile_shader is safe to
     * call concurrently thanks to the refcounted init. */

    /* Set up compile options (zero-init to avoid stack garbage) */
    PsbcCompileOptions opts = {0};
    opts.target = PSBC_TARGET_PS4_BASE;
    opts.entrypoint = "main";
    opts.optimise = true;

    PsbcDescriptorSetLayout psbc_sets[PSBC_MAX_DESCRIPTOR_SETS] = {0};
    PsbcDescriptorBinding psbc_bindings[PSBC_MAX_DESCRIPTOR_SETS]
                                             [PSBC_MAX_DESCRIPTOR_BINDINGS] = {0};
    if (layout) {
        if (layout->set_layout_count > PSBC_MAX_DESCRIPTOR_SETS)
            return VK_ERROR_FEATURE_NOT_PRESENT;
        opts.descriptor_set_count = layout->set_layout_count;
        opts.descriptor_sets = psbc_sets;
        opts.descriptor_address32_hi = VK_PS4_PSBC_DESCRIPTOR_ADDRESS32_HI;
        for (uint32_t set = 0; set < layout->set_layout_count; ++set) {
            const VkPs4DescriptorSetLayout *src = layout->set_layouts[set];
            if (!src || src->binding_count > PSBC_MAX_DESCRIPTOR_BINDINGS)
                return VK_ERROR_FEATURE_NOT_PRESENT;
            PsbcDescriptorSetLayout *dst = &psbc_sets[set];
            dst->binding_count = src->binding_count;
            dst->table_size = src->table_size;
            dst->dynamic_offset_count = src->dynamic_descriptor_count;
            dst->bindings = psbc_bindings[set];
            for (uint32_t i = 0; i < src->binding_count; ++i) {
                psbc_bindings[set][i] = (PsbcDescriptorBinding){
                    .binding = src->bindings[i].binding,
                    .descriptor_type = (uint32_t)src->bindings[i].descriptorType,
                    .array_size = src->bindings[i].descriptorCount,
                    .offset = src->binding_offsets[i],
                    .stride = src->binding_strides[i],
                    .dynamic_offset_offset = src->binding_dynamic_offsets[i],
                };
            }
        }
    }

    /* Determine stage */
    const char *stage_name = "unknown";
    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT: opts.stage = PSBC_STAGE_VERTEX; stage_name = "VS"; break;
    case VK_SHADER_STAGE_FRAGMENT_BIT: opts.stage = PSBC_STAGE_FRAGMENT; stage_name = "PS"; break;
    case VK_SHADER_STAGE_COMPUTE_BIT: opts.stage = PSBC_STAGE_COMPUTE; stage_name = "CS"; break;
    case VK_SHADER_STAGE_GEOMETRY_BIT: opts.stage = PSBC_STAGE_GEOMETRY; stage_name = "GS"; break;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: opts.stage = PSBC_STAGE_TESS_CTRL; stage_name = "TCS"; break;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: opts.stage = PSBC_STAGE_TESS_EVAL; stage_name = "TES"; break;
    default:
        vk_ps4_log("compile_shader: unsupported stage %u", (unsigned)stage);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    vk_ps4_log("compile_shader: %s spirv=%zu bytes descriptor_sets=%u",
               stage_name, mod->binary_size, opts.descriptor_set_count);

    /* Compile */
    PsbcShaderOutput output = {0};
    PsbcResult result = psbc_compile_shader(
        mod->binary,        /* SPIR-V data */
        mod->binary_size,   /* SPIR-V size */
        &opts,
        &output
    );

    if (result != PSBC_RESULT_OK) {
        vk_ps4_log("compile_shader: %s FAILED psbc_result=%d", stage_name, (int)result);
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    vk_ps4_log("compile_shader: %s OK binary=%zu bytes", stage_name, output.size);

    /* Copy the compiled binary */
    void *binary_copy = vk_ps4_alloc(alloc, output.size, 16);
    if (!binary_copy) {
        psbc_free_output(&output);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(binary_copy, output.data, output.size);

    *out_binary = binary_copy;
    *out_binary_size = output.size;

    /* Extract metadata from the compiled binary */
    if (out_metadata) {
        GnmError gnm_err = sceGnmShaderBinaryGetMetadata(
            binary_copy, output.size, out_metadata
        );
        if (gnm_err != GNM_ERROR_OK) {
            vk_ps4_log("compile_shader: %s metadata FAILED rc=%d",
                       stage_name, (int)gnm_err);
            memset(out_metadata, 0, sizeof(*out_metadata));
            vk_ps4_free(alloc, binary_copy);
            *out_binary = NULL;
            *out_binary_size = 0;
            psbc_free_output(&output);
            return VK_ERROR_INVALID_SHADER_NV;
        }
        if ((stage == VK_SHADER_STAGE_VERTEX_BIT ||
             stage == VK_SHADER_STAGE_FRAGMENT_BIT) &&
            (!out_metadata->shadercode || out_metadata->shadercodesize == 0)) {
            vk_ps4_log("compile_shader: %s metadata has no executable code",
                       stage_name);
            memset(out_metadata, 0, sizeof(*out_metadata));
            vk_ps4_free(alloc, binary_copy);
            *out_binary = NULL;
            *out_binary_size = 0;
            psbc_free_output(&output);
            return VK_ERROR_INVALID_SHADER_NV;
        }
        vk_ps4_log("compile_shader: %s container=%p code=%p code_size=%u code_low8=0x%x",
                   stage_name, binary_copy, out_metadata->shadercode,
                   out_metadata->shadercodesize,
                   (unsigned)((uintptr_t)out_metadata->shadercode & 255u));
    }

    psbc_free_output(&output);
    return VK_SUCCESS;
#else
    (void)stage;
    (void)out_metadata;
    /* No libpsbc — return the raw SPIR-V (stub mode, won't work on real GPU) */
    *out_binary = mod->binary;
    *out_binary_size = mod->binary_size;
    return VK_SUCCESS;
#endif
}
