/* Mesa compatibility hooks required by the bundled PSBC archive.
 * PSBC executes on the PS4's x86-64 CPU. Its BLAKE3 dispatcher can select SSE;
 * those paths must hash correctly even though no SIMD objects were packaged.
 * Route every missing SIMD entry point to the real portable implementation
 * already included in libpsbc.orbis.a. No data or hashing is discarded.
 */
#include "vk_ps4_blake3_compat.h"

void _mesa_log_multiline(const char *tag, const char *line) {
    (void)tag;
    (void)line;
}

#define COMPRESS_FORWARD(suffix) \
void blake3_compress_in_place_##suffix(uint32_t cv[8], const uint8_t block[64], \
        uint8_t block_len, uint64_t counter, uint8_t flags) { \
    blake3_compress_in_place_portable(cv, block, block_len, counter, flags); \
} \
void blake3_compress_xof_##suffix(const uint32_t cv[8], const uint8_t block[64], \
        uint8_t block_len, uint64_t counter, uint8_t flags, uint8_t out[64]) { \
    blake3_compress_xof_portable(cv, block, block_len, counter, flags, out); \
}
COMPRESS_FORWARD(sse2)
COMPRESS_FORWARD(sse41)
COMPRESS_FORWARD(avx512)
#undef COMPRESS_FORWARD

#define HASH_MANY_FORWARD(suffix) \
void blake3_hash_many_##suffix(const uint8_t *const *inputs, size_t num_inputs, \
        size_t blocks, const uint32_t key[8], uint64_t counter, \
        bool increment_counter, uint8_t flags, uint8_t flags_start, \
        uint8_t flags_end, uint8_t *out) { \
    blake3_hash_many_portable(inputs, num_inputs, blocks, key, counter, \
        increment_counter, flags, flags_start, flags_end, out); \
}
HASH_MANY_FORWARD(sse2)
HASH_MANY_FORWARD(sse41)
HASH_MANY_FORWARD(avx2)
HASH_MANY_FORWARD(avx512)
#undef HASH_MANY_FORWARD

void blake3_xof_many_avx512(const uint32_t cv[8], const uint8_t block[64],
        uint8_t block_len, uint64_t counter, uint8_t flags, uint8_t *out,
        size_t outblocks) {
    for (size_t i = 0; i < outblocks; ++i)
        blake3_compress_xof_portable(cv, block, block_len, counter + i,
                                    flags, out + 64 * i);
}
