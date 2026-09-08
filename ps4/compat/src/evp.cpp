/* WoWee PS4 crypto shim - EVP message digest layer. */
#include "shim_internal.hpp"
#include "openssl/sha.h"
#include "openssl/md5.h"

#include <cstring>
#include <new>

WOWEE_SHIM_BEGIN

namespace {

// static as well as in the unnamed namespace: WOWEE_SHIM_BEGIN opens an
// extern "C" block, under which these would keep their unmangled global
// names and collide with libtomcrypt's sha1_init / md5_init, which the
// vendored StormLib (linked into the client for the on-console extraction)
// brings into the same link.
static void sha1_init(void *c) { SHA1_Init(static_cast<SHA_CTX *>(c)); }
static void sha1_update(void *c, const void *d, size_t n) { SHA1_Update(static_cast<SHA_CTX *>(c), d, n); }
static void sha1_final(unsigned char *md, void *c) { SHA1_Final(md, static_cast<SHA_CTX *>(c)); }

static void sha256_init(void *c) { SHA256_Init(static_cast<SHA256_CTX *>(c)); }
static void sha256_update(void *c, const void *d, size_t n) { SHA256_Update(static_cast<SHA256_CTX *>(c), d, n); }
static void sha256_final(unsigned char *md, void *c) { SHA256_Final(md, static_cast<SHA256_CTX *>(c)); }

static void md5_init(void *c) { MD5_Init(static_cast<MD5_CTX *>(c)); }
static void md5_update(void *c, const void *d, size_t n) { MD5_Update(static_cast<MD5_CTX *>(c), d, n); }
static void md5_final(unsigned char *md, void *c) { MD5_Final(md, static_cast<MD5_CTX *>(c)); }

const evp_md_st kSha1   = {64,  "SHA1",   SHA_DIGEST_LENGTH,    SHA_CBLOCK,    sizeof(SHA_CTX),    sha1_init,   sha1_update,   sha1_final};
const evp_md_st kSha256 = {672, "SHA256", SHA256_DIGEST_LENGTH, SHA256_CBLOCK, sizeof(SHA256_CTX), sha256_init, sha256_update, sha256_final};
const evp_md_st kMd5    = {4,   "MD5",    MD5_DIGEST_LENGTH,    MD5_CBLOCK,    sizeof(MD5_CTX),    md5_init,    md5_update,    md5_final};

constexpr size_t kMaxCtx = 128;
static_assert(sizeof(SHA_CTX) <= kMaxCtx && sizeof(SHA256_CTX) <= kMaxCtx && sizeof(MD5_CTX) <= kMaxCtx,
              "digest context does not fit in EVP_MD_CTX storage");

bool ieq(const char *a, const char *b) {
    for (;; ++a, ++b) {
        int ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        int cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
        if (ca != cb) return false;
        if (!ca) return true;
    }
}

} // namespace

struct evp_md_ctx_st {
    const EVP_MD *md = nullptr;
    alignas(16) unsigned char state[kMaxCtx];
};

const EVP_MD *EVP_sha1(void) { return &kSha1; }
const EVP_MD *EVP_sha256(void) { return &kSha256; }
const EVP_MD *EVP_md5(void) { return &kMd5; }

const EVP_MD *EVP_get_digestbyname(const char *name) {
    if (!name) return nullptr;
    if (ieq(name, "SHA1") || ieq(name, "SHA-1")) return &kSha1;
    if (ieq(name, "SHA256") || ieq(name, "SHA-256") || ieq(name, "SHA2-256")) return &kSha256;
    if (ieq(name, "MD5")) return &kMd5;
    return nullptr;
}

int EVP_MD_get_size(const EVP_MD *md) { return md ? static_cast<int>(md->md_size) : -1; }
int EVP_MD_get_block_size(const EVP_MD *md) { return md ? static_cast<int>(md->block_size) : -1; }

EVP_MD_CTX *EVP_MD_CTX_new(void) { return new (std::nothrow) evp_md_ctx_st(); }
void EVP_MD_CTX_free(EVP_MD_CTX *ctx) {
    if (!ctx) return;
    OPENSSL_cleanse(ctx->state, sizeof ctx->state);
    delete ctx;
}
int EVP_MD_CTX_reset(EVP_MD_CTX *ctx) {
    if (!ctx) return 0;
    ctx->md = nullptr;
    std::memset(ctx->state, 0, sizeof ctx->state);
    return 1;
}
int EVP_MD_CTX_copy_ex(EVP_MD_CTX *out, const EVP_MD_CTX *in) {
    if (!out || !in) return 0;
    *out = *in;
    return 1;
}
const EVP_MD *EVP_MD_CTX_get0_md(const EVP_MD_CTX *ctx) { return ctx ? ctx->md : nullptr; }

int EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *) {
    if (!ctx) return 0;
    if (!type) type = ctx->md;
    if (!type) return 0;
    ctx->md = type;
    type->init(ctx->state);
    return 1;
}
int EVP_DigestInit(EVP_MD_CTX *ctx, const EVP_MD *type) { return EVP_DigestInit_ex(ctx, type, nullptr); }

int EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt) {
    if (!ctx || !ctx->md) return 0;
    if (cnt && !d) return 0;
    ctx->md->update(ctx->state, d, cnt);
    return 1;
}

int EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s) {
    if (!ctx || !ctx->md || !md) return 0;
    ctx->md->final(md, ctx->state);
    if (s) *s = ctx->md->md_size;
    return 1;
}
int EVP_DigestFinal(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s) {
    int r = EVP_DigestFinal_ex(ctx, md, s);
    EVP_MD_CTX_reset(ctx);
    return r;
}

int EVP_Digest(const void *data, size_t count, unsigned char *md, unsigned int *size,
               const EVP_MD *type, ENGINE *) {
    if (!type || !md) return 0;
    if (count && !data) return 0;
    alignas(16) unsigned char state[kMaxCtx];
    type->init(state);
    type->update(state, data, count);
    type->final(md, state);
    if (size) *size = type->md_size;
    OPENSSL_cleanse(state, sizeof state);
    return 1;
}

WOWEE_SHIM_END
