#ifndef VK_PS4_TEXTURE_ADDRESS_H
#define VK_PS4_TEXTURE_ADDRESS_H

#include <stdbool.h>
#include <stdint.h>

#include "gnm_texture.h"

/*
 * A PS4 T# stores a byte address divided by 256 across baseaddress (bits
 * 8..39) and baseaddresshi (bits 40..45).  The pinned OpenGNM FW-5.05
 * sceGnmTexSetBaseAddress implementation writes only the low field.  Direct
 * memory mappings routinely use the upper field, so leaving it at zero makes
 * the texture unit read an unrelated address even though buffer fetches work.
 */
static inline bool vk_ps4_texture_address_is_encodable(const void *address) {
    const uintptr_t value = (uintptr_t)address;
    return address != NULL && (value & 0xffu) == 0u && (value >> 46u) == 0u;
}

/* Call after sceGnmTexSetBaseAddress has populated/swizzled the low field. */
static inline bool vk_ps4_texture_finalize_base_address(
    GnmTexture *texture, const void *address
) {
    if (!texture || !vk_ps4_texture_address_is_encodable(address)) {
        return false;
    }
    const uintptr_t value = (uintptr_t)address;
    texture->baseaddresshi = (uint32_t)((value >> 40u) & 0x3fu);
    sceGnmTexSetMemoryType(texture, GNM_MEMORY_READONLY, false);
    return true;
}

static inline uintptr_t vk_ps4_texture_decoded_base_address(
    const GnmTexture *texture
) {
    if (!texture) return 0u;
    return ((uintptr_t)texture->baseaddress << 8u) |
           ((uintptr_t)texture->baseaddresshi << 40u);
}

/* SQ_IMG_RSRC_WORD5 stores an inclusive array-slice range.  The pinned
 * OpenGNM CreateTexture implementation assigns numslices directly to
 * lastarray, producing [0,1] for a non-array 2D texture. */
static inline bool vk_ps4_texture_normalize_array_view(
    GnmTexture *texture, uint32_t array_layers
) {
    if (!texture || array_layers == 0u || array_layers > 8192u) return false;
    texture->basearray = 0u;
    texture->lastarray = array_layers - 1u;
    return true;
}

#endif /* VK_PS4_TEXTURE_ADDRESS_H */
