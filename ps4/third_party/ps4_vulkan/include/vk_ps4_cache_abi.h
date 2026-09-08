#ifndef VK_PS4_CACHE_ABI_H
#define VK_PS4_CACHE_ABI_H

#include <stdint.h>

#define VK_PS4_CACHE_VENDOR_ID 0x1002u
#define VK_PS4_CACHE_DEVICE_ID 0x9920u
#define VK_PS4_CACHE_HEADER_SIZE 32u
#define VK_PS4_CACHE_RECORD_SIZE 20u /* hash64, stage32, SPIR-V size32, binary size32 */

/* B9 PSBC PARAM-only exports + flat interpolation metadata, serialized records
 * v1 (20 bytes). Deliberate compiler ABI identity, shared by device properties
 * and cache serialization. Bump whenever emitted ISA/metadata/cache layout
 * changes incompatibly; neither B7's ATTR0-only shaders nor B8's old raster
 * semantics may be reused. Derived from the B9 PSBC archive SHA-256 prefix. */
static const uint8_t vk_ps4_pipeline_cache_uuid[16] = {
    0xf6, 0xbd, 0x82, 0x0d, 0x34, 0x43, 0x3f, 0x26,
    0x19, 0x5a, 0x23, 0x07, 0x43, 0x6d, 0x7a, 0x7f
};

#endif
