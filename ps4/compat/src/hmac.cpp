/* WoWee PS4 crypto shim - HMAC (RFC 2104) over any shim EVP_MD. */
#include "shim_internal.hpp"
#include "openssl/hmac.h"

#include <cstring>
#include <new>

WOWEE_SHIM_BEGIN

struct hmac_ctx_st {
    const EVP_MD *md = nullptr;
    alignas(16) unsigned char inner[128];
    alignas(16) unsigned char outer[128];
    unsigned char ipadKey[HMAC_MAX_MD_CBLOCK_SIZE];
    unsigned char opadKey[HMAC_MAX_MD_CBLOCK_SIZE];
};

HMAC_CTX *HMAC_CTX_new(void) { return new (std::nothrow) hmac_ctx_st(); }

void HMAC_CTX_free(HMAC_CTX *ctx) {
    if (!ctx) return;
    OPENSSL_cleanse(ctx, sizeof(*ctx));
    delete ctx;
}

int HMAC_CTX_reset(HMAC_CTX *ctx) {
    if (!ctx) return 0;
    OPENSSL_cleanse(ctx, sizeof(*ctx));
    ctx->md = nullptr;
    return 1;
}

int HMAC_Init_ex(HMAC_CTX *ctx, const void *key, int len, const EVP_MD *md, ENGINE *) {
    if (!ctx) return 0;
    if (!md) md = ctx->md;
    if (!md) return 0;
    if (md->block_size > HMAC_MAX_MD_CBLOCK_SIZE || md->ctx_size > sizeof ctx->inner) return 0;

    if (key || md != ctx->md) {
        /* (Re)derive the padded keys.  key == NULL with the same md reuses them. */
        if (!key) len = 0;
        if (len < 0) return 0;
        unsigned char kbuf[EVP_MAX_MD_SIZE];
        const unsigned char *k = static_cast<const unsigned char *>(key);
        size_t klen = static_cast<size_t>(len);
        if (klen > md->block_size) {
            alignas(16) unsigned char st[128];
            md->init(st);
            md->update(st, k, klen);
            md->final(kbuf, st);
            k = kbuf;
            klen = md->md_size;
        }
        std::memset(ctx->ipadKey, 0x36, md->block_size);
        std::memset(ctx->opadKey, 0x5c, md->block_size);
        for (size_t i = 0; i < klen; ++i) {
            ctx->ipadKey[i] ^= k[i];
            ctx->opadKey[i] ^= k[i];
        }
        ctx->md = md;
    }
    md->init(ctx->inner);
    md->update(ctx->inner, ctx->ipadKey, md->block_size);
    md->init(ctx->outer);
    md->update(ctx->outer, ctx->opadKey, md->block_size);
    return 1;
}

int HMAC_Update(HMAC_CTX *ctx, const unsigned char *data, size_t len) {
    if (!ctx || !ctx->md) return 0;
    if (len && !data) return 0;
    ctx->md->update(ctx->inner, data, len);
    return 1;
}

int HMAC_Final(HMAC_CTX *ctx, unsigned char *md, unsigned int *len) {
    if (!ctx || !ctx->md || !md) return 0;
    unsigned char innerDigest[EVP_MAX_MD_SIZE];
    ctx->md->final(innerDigest, ctx->inner);
    ctx->md->update(ctx->outer, innerDigest, ctx->md->md_size);
    ctx->md->final(md, ctx->outer);
    if (len) *len = ctx->md->md_size;
    return 1;
}

size_t HMAC_size(const HMAC_CTX *ctx) { return (ctx && ctx->md) ? ctx->md->md_size : 0; }

unsigned char *HMAC(const EVP_MD *evp_md, const void *key, int key_len,
                    const unsigned char *data, size_t data_len,
                    unsigned char *md, unsigned int *md_len) {
    static unsigned char m[EVP_MAX_MD_SIZE];
    if (!evp_md) return nullptr;
    if (!md) md = m;
    hmac_ctx_st ctx;
    /* HMAC() accepts key == NULL as an empty key (OpenSSL treats it as ""). */
    const unsigned char empty = 0;
    if (!HMAC_Init_ex(&ctx, key ? key : &empty, key ? key_len : 0, evp_md, nullptr)) return nullptr;
    if (!HMAC_Update(&ctx, data, data_len)) return nullptr;
    if (!HMAC_Final(&ctx, md, md_len)) return nullptr;
    OPENSSL_cleanse(&ctx, sizeof ctx);
    return md;
}

WOWEE_SHIM_END
