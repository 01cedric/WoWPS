#ifndef VK_PS4_VERTEX_HASH_H
#define VK_PS4_VERTEX_HASH_H

#include <stddef.h>
#include <stdint.h>
#include <gnm.h>

/* CPU-only hash of the initialized semantic prefix. The caller still checks
 * the entire zero-padded key for equality, so hash collisions never alias
 * descriptors. Including the count separates distinct prefix lengths. */
static inline uint32_t vk_ps4_vertex_table_hash(const GnmBuffer *descriptors,
                                               uint32_t semantic_count) {
    uint32_t hash = (2166136261u ^ semantic_count) * 16777619u;
    const unsigned char *bytes = (const unsigned char *)descriptors;
    const size_t size = (size_t)semantic_count * sizeof(GnmBuffer);
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

#endif
