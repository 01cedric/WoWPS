#ifndef VK_PS4_COMPUTE_SHADER_H
#define VK_PS4_COMPUTE_SHADER_H

#include "gnm_helpers.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct VkPs4ComputeShader {
    GnmCsStageRegisters registers;
    const GnmInputUsageSlot *slots;
    uint32_t slot_count;
    const void *code;
    uint32_t code_size;
} VkPs4ComputeShader;

static inline bool vk_ps4_shader_span(const void *binary, size_t size,
                                      const void *part, size_t bytes) {
    const uintptr_t start = (uintptr_t)binary, address = (uintptr_t)part;
    return binary && part && size <= UINTPTR_MAX - start &&
           address >= start && address - start <= size &&
           bytes <= size - (address - start);
}

/* Older GNM metadata parsers expose CS common data but leave stage/code/slots
 * unset. Decode the complete CS stage header, not just its common prefix.
 * headersizedwords includes the stage registers AND resource usage slots.
 * The compiler's code address is relative and is replaced after GPU upload. */
static inline const char *vk_ps4_decode_compute_shader(
    const void *binary, size_t size, const GnmShaderMetadata *metadata,
    uint32_t max_slots, VkPs4ComputeShader *out
) {
    if (!out) return "missing output";
    memset(out, 0, sizeof(*out));
    if (!metadata || !vk_ps4_shader_span(binary, size, metadata->fileheader,
                                         sizeof(GnmShaderFileHeader)) ||
        !vk_ps4_shader_span(binary, size, metadata->common, sizeof(GnmCsShader)))
        return "truncated CS header";
    GnmShaderFileHeader header;
    GnmCsShader shader;
    memcpy(&header, metadata->fileheader, sizeof(header));
    memcpy(&shader, metadata->common, sizeof(shader));
    if (header.magic != GNM_SHADER_FILE_HEADER_ID || header.type != GNM_SHADER_COMPUTE ||
        metadata->type != GNM_SHADER_COMPUTE)
        return "not a CS container";
    const uint32_t count = shader.common.numinputusageslots;
    const uint32_t header_bytes = (uint32_t)header.headersizedwords * 4u;
    const uint32_t minimum_header = sizeof(GnmCsShader) + count * sizeof(GnmInputUsageSlot);
    if (count > max_slots || header_bytes < minimum_header ||
        !vk_ps4_shader_span(binary, size, metadata->common, header_bytes))
        return "invalid CS resource table span";
    const uint8_t *base = (const uint8_t *)metadata->common;
    const uint32_t declared_bytes = sceGnmShaderCommonCodeSize(&shader.common);
    // Wrapped containers include an OrbShdr trailer in the common size; the
    // metadata parser extracts the actual instruction length from that trailer.
    const uint32_t code_bytes = metadata->shadercodesize;
    if (!code_bytes || code_bytes > declared_bytes || (code_bytes & 3u) ||
        !vk_ps4_shader_span(binary, size, base + header_bytes, code_bytes))
        return "truncated CS code";
    const GnmCsStageRegisters *regs = &shader.registers;
    const uint32_t x = regs->computenumthreadx, y = regs->computenumthready,
                   z = regs->computenumthreadz;
    if (!x || !y || !z || x > 1024 || y > 1024 || z > 1024 ||
        (uint64_t)x * y * z > 1024)
        return "invalid CS workgroup dimensions";
    // This ICD has no scratch-ring allocation/binding. Reject rather than
    // execute a shader that would access an unbound scratch address.
    if (shader.common.scratchsizeperthreaddwords || (regs->computepgmrsrc2 & 1u))
        return "CS scratch storage unsupported";
    out->registers = *regs;
    out->slots = (const GnmInputUsageSlot *)(base + sizeof(GnmCsShader));
    out->slot_count = count;
    out->code = base + header_bytes;
    out->code_size = code_bytes;
    return NULL;
}
#endif
