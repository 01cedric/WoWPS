/*
 * WoWee PS4 crypto shim - private definitions shared by the shim sources.
 * Not part of the OpenSSL-compatible surface; do not include from WoWee code.
 */
#ifndef WOWEE_PS4_COMPAT_SHIM_INTERNAL_HPP
#define WOWEE_PS4_COMPAT_SHIM_INTERNAL_HPP

#include "openssl/bn.h"
#include "openssl/evp.h"

#include <cstdint>
#include <cstddef>
#include <vector>

WOWEE_SHIM_BEGIN

/* Little-endian base-2^32 limbs, always normalized (no most-significant zero limb;
 * zero is the empty vector and is never negative). */
struct bignum_st {
    std::vector<uint32_t> d;
    bool neg = false;
};

struct bignum_ctx {
    int depth = 0;
    std::vector<BIGNUM *> pool;
};

/* Digest algorithm descriptor behind the opaque EVP_MD. */
struct evp_md_st {
    int nid;
    const char *name;
    unsigned int md_size;
    unsigned int block_size;
    size_t ctx_size;
    void (*init)(void *ctx);
    void (*update)(void *ctx, const void *data, size_t len);
    void (*final)(unsigned char *md, void *ctx);
};

/* Logging helper (stderr); the shim must not depend on WoWee's logger. */
void wowee_shim_log(const char *fmt, ...);

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_SHIM_INTERNAL_HPP */
