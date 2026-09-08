#ifndef VK_PS4_BLAKE3_COMPAT_H
#define VK_PS4_BLAKE3_COMPAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* BLAKE3 C ABI, including its 64-bit counter and all hash_many flags.
 * https://github.com/BLAKE3-team/BLAKE3/blob/1.5.5/c/blake3_impl.h */
#define PS4_BLAKE3_COMPRESS_DECL(suffix) \
void blake3_compress_in_place_##suffix(uint32_t cv[8], const uint8_t block[64], uint8_t block_len, uint64_t counter, uint8_t flags); \
void blake3_compress_xof_##suffix(const uint32_t cv[8], const uint8_t block[64], uint8_t block_len, uint64_t counter, uint8_t flags, uint8_t out[64]);
#define PS4_BLAKE3_MANY_DECL(suffix) \
void blake3_hash_many_##suffix(const uint8_t *const *inputs, size_t num_inputs, size_t blocks, const uint32_t key[8], uint64_t counter, bool increment_counter, uint8_t flags, uint8_t flags_start, uint8_t flags_end, uint8_t *out);
PS4_BLAKE3_COMPRESS_DECL(portable)
PS4_BLAKE3_COMPRESS_DECL(sse2)
PS4_BLAKE3_COMPRESS_DECL(sse41)
PS4_BLAKE3_COMPRESS_DECL(avx512)
PS4_BLAKE3_MANY_DECL(portable)
PS4_BLAKE3_MANY_DECL(sse2)
PS4_BLAKE3_MANY_DECL(sse41)
PS4_BLAKE3_MANY_DECL(avx2)
PS4_BLAKE3_MANY_DECL(avx512)
void blake3_xof_many_avx512(const uint32_t cv[8], const uint8_t block[64], uint8_t block_len, uint64_t counter, uint8_t flags, uint8_t *out, size_t outblocks);
#undef PS4_BLAKE3_COMPRESS_DECL
#undef PS4_BLAKE3_MANY_DECL
#endif
