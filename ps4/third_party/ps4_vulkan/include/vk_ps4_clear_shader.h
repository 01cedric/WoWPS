#ifndef VK_PS4_CLEAR_SHADER_H
#define VK_PS4_CLEAR_SHADER_H
#include "gnm_shaderbinary.h"
#include "psbc_compile.h"
#include <stdbool.h>
#include <stdint.h>

/* Exactly one vec4 UBO, set 0 / binding 0. The descriptor layout is required:
 * compiling SPIR-V without it can silently lower the UBO to a null resource.
 * Keep the host compiler probe and runtime on this identical contract. */
static inline PsbcCompileOptions vk_ps4_clear_compile_options(uint32_t address_hi) {
    static const PsbcDescriptorBinding binding = {
        .binding = 0, .descriptor_type = 6, /* VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER */
        .array_size = 1, .offset = 0, .stride = 16, .dynamic_offset_offset = 0
    };
    static const PsbcDescriptorSetLayout set = {
        .binding_count = 1, .table_size = 16, .dynamic_offset_count = 0,
        .bindings = &binding
    };
    return (PsbcCompileOptions){
        .target = PSBC_TARGET_PS4_BASE, .stage = PSBC_STAGE_FRAGMENT,
        .entrypoint = "main", .optimise = true, .descriptor_set_count = 1,
        .descriptor_sets = &set, .descriptor_address32_hi = address_hi
    };
}

/* Bind a table pointer at the actual SGPR reported by the compiler.
 * Never substitute an inline V# or scalar color values. */
static inline bool vk_ps4_clear_table_register(const GnmPsShader *ps, uint8_t *reg) {
    if (!ps || !reg || ps->numinputsemantics != 0 || ps->common.numinputusageslots != 1)
        return false;
    const GnmInputUsageSlot *slot = sceGnmPsShaderInputUsageSlotTable(ps);
    if (slot->usagetype != GNM_SHINPUTUSAGE_PTR_INDIRECTRESOURCETABLE ||
        slot->apislot != 0 || slot->startregister >= 16) return false;
    *reg = slot->startregister;
    return true;
}
#endif
