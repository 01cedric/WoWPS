#ifndef VK_PS4_DISPATCH_H
#define VK_PS4_DISPATCH_H

#include "gnm_commandbuffer.h"
#include <pm4/sid.h>

/* DISPATCH_DIRECT has four payload words: X, Y, Z, initiator.
 * PM4 type-3 COUNT is payload words minus one, so the total is five
 * words and COUNT must be 3. The pinned OpenGNM wrapper writes COUNT=4
 * followed by a NOP at word 5: the CP consumes that NOP header as data
 * and interprets its zero payload as commands. Emit this packet here
 * instead of using that wrapper. No trailing padding is required.
 * Keep allocation atomic; the ICD rejects a recording that cannot fit. */
static inline bool vk_ps4_emit_dispatch_direct(
    GnmCommandBuffer *cmd, uint32_t x, uint32_t y, uint32_t z
) {
    if (!cmd || !cmd->cmdptr || !cmd->endptr ||
        cmd->cmdptr > cmd->endptr || cmd->endptr - cmd->cmdptr < 5)
        return false;
    cmd->cmdptr[0] = PKT3(PKT3_DISPATCH_DIRECT, 3, 0) | PKT3_SHADER_TYPE_S(1);
    cmd->cmdptr[1] = x;
    cmd->cmdptr[2] = y;
    cmd->cmdptr[3] = z;
    cmd->cmdptr[4] = 1u; /* COMPUTE_SHADER_EN; dimensions are workgroup counts */
    cmd->cmdptr += 5;
    return true;
}

#endif
