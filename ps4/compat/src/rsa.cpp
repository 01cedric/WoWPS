/*
 * WoWee PS4 crypto shim - RSA public-key operations behind the OpenSSL 3
 * provider-style API used by WardenModule::verifyRSASignature:
 *
 *   OSSL_PARAM_BLD (n, e) -> EVP_PKEY_fromdata -> EVP_PKEY_verify_recover
 *
 * verify-recover is the textbook RSA public operation sig^e mod n.  With
 * RSA_NO_PADDING the result is returned as exactly RSA_size(n) bytes; with
 * RSA_PKCS1_PADDING the block-type-1 padding is removed.
 */
#include "shim_internal.hpp"
#include "openssl/core_names.h"
#include "openssl/param_build.h"
#include "openssl/params.h"
#include "openssl/rsa.h"

#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

WOWEE_SHIM_BEGIN

/* ---- OSSL_PARAM / OSSL_PARAM_BLD -------------------------------------- */

struct ossl_param_bld_st {
    struct Item {
        std::string key;
        unsigned int type;
        std::vector<unsigned char> data; /* native (little-endian) as in OpenSSL */
    };
    std::vector<Item> items;
};

OSSL_PARAM_BLD *OSSL_PARAM_BLD_new(void) { return new (std::nothrow) ossl_param_bld_st(); }

void OSSL_PARAM_BLD_free(OSSL_PARAM_BLD *bld) { delete bld; }

int OSSL_PARAM_BLD_push_BN(OSSL_PARAM_BLD *bld, const char *key, const BIGNUM *bn) {
    if (!bld || !key || !bn) return 0;
    if (BN_is_negative(bn)) return 0;
    ossl_param_bld_st::Item item;
    item.key = key;
    item.type = OSSL_PARAM_UNSIGNED_INTEGER;
    int n = BN_num_bytes(bn);
    if (n == 0) n = 1;
    item.data.assign(static_cast<size_t>(n), 0);
    if (BN_bn2lebinpad(bn, item.data.data(), n) < 0) return 0;
    bld->items.push_back(std::move(item));
    return 1;
}

int OSSL_PARAM_BLD_push_uint(OSSL_PARAM_BLD *bld, const char *key, unsigned int val) {
    if (!bld || !key) return 0;
    ossl_param_bld_st::Item item;
    item.key = key;
    item.type = OSSL_PARAM_UNSIGNED_INTEGER;
    item.data.resize(sizeof val);
    std::memcpy(item.data.data(), &val, sizeof val);
    bld->items.push_back(std::move(item));
    return 1;
}

OSSL_PARAM *OSSL_PARAM_BLD_to_param(OSSL_PARAM_BLD *bld) {
    if (!bld) return nullptr;
    const size_t n = bld->items.size();
    size_t total = (n + 1) * sizeof(OSSL_PARAM);
    for (const auto &it : bld->items) total += it.key.size() + 1 + it.data.size() + 8;
    unsigned char *block = static_cast<unsigned char *>(OPENSSL_zalloc(total));
    if (!block) return nullptr;
    OSSL_PARAM *params = reinterpret_cast<OSSL_PARAM *>(block);
    unsigned char *cursor = block + (n + 1) * sizeof(OSSL_PARAM);
    for (size_t i = 0; i < n; ++i) {
        const auto &it = bld->items[i];
        std::memcpy(cursor, it.key.c_str(), it.key.size() + 1);
        params[i].key = reinterpret_cast<const char *>(cursor);
        cursor += it.key.size() + 1;
        cursor = reinterpret_cast<unsigned char *>((reinterpret_cast<uintptr_t>(cursor) + 7u) & ~static_cast<uintptr_t>(7u));
        if (!it.data.empty()) std::memcpy(cursor, it.data.data(), it.data.size());
        params[i].data_type = it.type;
        params[i].data = cursor;
        params[i].data_size = it.data.size();
        params[i].return_size = 0;
        cursor += it.data.size();
    }
    params[n].key = nullptr; /* terminator */
    return params;
}

void OSSL_PARAM_free(OSSL_PARAM *params) { OPENSSL_free(params); }

OSSL_PARAM *OSSL_PARAM_locate(OSSL_PARAM *params, const char *key) {
    if (!params || !key) return nullptr;
    for (OSSL_PARAM *p = params; p->key; ++p)
        if (std::strcmp(p->key, key) == 0) return p;
    return nullptr;
}

const OSSL_PARAM *OSSL_PARAM_locate_const(const OSSL_PARAM *params, const char *key) {
    return OSSL_PARAM_locate(const_cast<OSSL_PARAM *>(params), key);
}

int OSSL_PARAM_get_BN(const OSSL_PARAM *p, BIGNUM **val) {
    if (!p || !val || !p->data) return 0;
    if (p->data_type != OSSL_PARAM_UNSIGNED_INTEGER) return 0;
    BIGNUM *r = BN_lebin2bn(static_cast<const unsigned char *>(p->data), static_cast<int>(p->data_size), *val);
    if (!r) return 0;
    *val = r;
    return 1;
}

/* ---- EVP_PKEY / EVP_PKEY_CTX ------------------------------------------ */

struct evp_pkey_st {
    int refs = 1;
    BIGNUM *n = nullptr;
    BIGNUM *e = nullptr;
};

enum PkeyOp { OP_NONE = 0, OP_FROMDATA, OP_VERIFY_RECOVER, OP_ENCRYPT };

struct evp_pkey_ctx_st {
    EVP_PKEY *pkey = nullptr;
    int op = OP_NONE;
    int padding = RSA_PKCS1_PADDING;
    bool isRsa = false;
};

EVP_PKEY_CTX *EVP_PKEY_CTX_new_from_name(OSSL_LIB_CTX *, const char *name, const char *) {
    if (!name) return nullptr;
    std::string n(name);
    for (char &c : n) c = static_cast<char>((c >= 'a' && c <= 'z') ? c - 32 : c);
    if (n != "RSA" && n != "RSA-PSS") {
        wowee_shim_log("EVP_PKEY_CTX_new_from_name: unsupported key type '%s' (only RSA)", name);
        return nullptr;
    }
    evp_pkey_ctx_st *ctx = new (std::nothrow) evp_pkey_ctx_st();
    if (ctx) ctx->isRsa = true;
    return ctx;
}

EVP_PKEY_CTX *EVP_PKEY_CTX_new_id(int id, ENGINE *) {
    if (id != EVP_PKEY_RSA) {
        wowee_shim_log("EVP_PKEY_CTX_new_id: unsupported key type %d (only RSA)", id);
        return nullptr;
    }
    return EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
}

EVP_PKEY_CTX *EVP_PKEY_CTX_new(EVP_PKEY *pkey, ENGINE *) {
    if (!pkey) return nullptr;
    evp_pkey_ctx_st *ctx = new (std::nothrow) evp_pkey_ctx_st();
    if (!ctx) return nullptr;
    EVP_PKEY_up_ref(pkey);
    ctx->pkey = pkey;
    ctx->isRsa = true;
    return ctx;
}

void EVP_PKEY_CTX_free(EVP_PKEY_CTX *ctx) {
    if (!ctx) return;
    EVP_PKEY_free(ctx->pkey);
    delete ctx;
}

int EVP_PKEY_fromdata_init(EVP_PKEY_CTX *ctx) {
    if (!ctx || !ctx->isRsa) return 0;
    ctx->op = OP_FROMDATA;
    return 1;
}

int EVP_PKEY_fromdata(EVP_PKEY_CTX *ctx, EVP_PKEY **ppkey, int selection, OSSL_PARAM params[]) {
    if (!ctx || ctx->op != OP_FROMDATA || !ppkey || !params) return 0;
    const OSSL_PARAM *pn = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_N);
    const OSSL_PARAM *pe = OSSL_PARAM_locate_const(params, OSSL_PKEY_PARAM_RSA_E);
    if (!pn) {
        wowee_shim_log("EVP_PKEY_fromdata: missing RSA modulus parameter '%s'", OSSL_PKEY_PARAM_RSA_N);
        return 0;
    }
    if (!pe && (selection & EVP_PKEY_PUBLIC_KEY) == EVP_PKEY_PUBLIC_KEY) {
        wowee_shim_log("EVP_PKEY_fromdata: missing RSA public exponent parameter '%s'", OSSL_PKEY_PARAM_RSA_E);
        return 0;
    }
    evp_pkey_st *key = new (std::nothrow) evp_pkey_st();
    if (!key) return 0;
    if (!OSSL_PARAM_get_BN(pn, &key->n) || (pe && !OSSL_PARAM_get_BN(pe, &key->e))) {
        EVP_PKEY_free(key);
        return 0;
    }
    if (BN_is_zero(key->n)) {
        wowee_shim_log("EVP_PKEY_fromdata: RSA modulus is zero");
        EVP_PKEY_free(key);
        return 0;
    }
    *ppkey = key;
    return 1;
}

void EVP_PKEY_free(EVP_PKEY *pkey) {
    if (!pkey) return;
    if (--pkey->refs > 0) return;
    BN_free(pkey->n);
    BN_free(pkey->e);
    delete pkey;
}

int EVP_PKEY_up_ref(EVP_PKEY *pkey) {
    if (!pkey) return 0;
    ++pkey->refs;
    return 1;
}

int EVP_PKEY_get_bits(const EVP_PKEY *pkey) { return (pkey && pkey->n) ? BN_num_bits(pkey->n) : 0; }
int EVP_PKEY_get_size(const EVP_PKEY *pkey) { return (pkey && pkey->n) ? BN_num_bytes(pkey->n) : 0; }
int EVP_PKEY_get_id(const EVP_PKEY *pkey) { return pkey ? EVP_PKEY_RSA : EVP_PKEY_NONE; }
int EVP_PKEY_get_base_id(const EVP_PKEY *pkey) { return EVP_PKEY_get_id(pkey); }

int EVP_PKEY_get_bn_param(const EVP_PKEY *pkey, const char *key_name, BIGNUM **bn) {
    if (!pkey || !key_name || !bn) return 0;
    const BIGNUM *src = nullptr;
    if (std::strcmp(key_name, OSSL_PKEY_PARAM_RSA_N) == 0) src = pkey->n;
    else if (std::strcmp(key_name, OSSL_PKEY_PARAM_RSA_E) == 0) src = pkey->e;
    if (!src) return 0;
    if (*bn) {
        if (!BN_copy(*bn, src)) return 0;
    } else {
        *bn = BN_dup(src);
        if (!*bn) return 0;
    }
    return 1;
}

int EVP_PKEY_verify_recover_init(EVP_PKEY_CTX *ctx) {
    if (!ctx || !ctx->pkey || !ctx->pkey->e) return 0;
    ctx->op = OP_VERIFY_RECOVER;
    ctx->padding = RSA_PKCS1_PADDING;
    return 1;
}

int EVP_PKEY_encrypt_init(EVP_PKEY_CTX *ctx) {
    if (!ctx || !ctx->pkey || !ctx->pkey->e) return 0;
    ctx->op = OP_ENCRYPT;
    ctx->padding = RSA_PKCS1_PADDING;
    return 1;
}

int EVP_PKEY_CTX_set_rsa_padding(EVP_PKEY_CTX *ctx, int pad_mode) {
    if (!ctx) return 0;
    if (pad_mode != RSA_NO_PADDING && pad_mode != RSA_PKCS1_PADDING) {
        wowee_shim_log("EVP_PKEY_CTX_set_rsa_padding: padding mode %d not supported (RSA_NO_PADDING / RSA_PKCS1_PADDING only)", pad_mode);
        return 0;
    }
    ctx->padding = pad_mode;
    return 1;
}

int EVP_PKEY_CTX_get_rsa_padding(EVP_PKEY_CTX *ctx, int *pad_mode) {
    if (!ctx || !pad_mode) return 0;
    *pad_mode = ctx->padding;
    return 1;
}

namespace {

/* out = in^e mod n as exactly RSA_size bytes.  Fails when in >= n. */
bool rsaPublicRaw(const EVP_PKEY *key, const unsigned char *in, size_t inlen, std::vector<unsigned char> &out) {
    const int size = BN_num_bytes(key->n);
    if (inlen != static_cast<size_t>(size)) {
        wowee_shim_log("RSA public op: input length %zu does not match modulus size %d", inlen, size);
        return false;
    }
    BIGNUM *m = BN_bin2bn(in, static_cast<int>(inlen), nullptr);
    BIGNUM *r = BN_new();
    bool ok = m && r;
    if (ok && BN_ucmp(m, key->n) >= 0) {
        wowee_shim_log("RSA public op: input is not smaller than the modulus");
        ok = false;
    }
    if (ok) ok = BN_mod_exp(r, m, key->e, key->n, nullptr) == 1;
    if (ok) {
        out.assign(static_cast<size_t>(size), 0);
        ok = BN_bn2binpad(r, out.data(), size) == size;
    }
    BN_free(m);
    BN_free(r);
    return ok;
}

} // namespace

int EVP_PKEY_verify_recover(EVP_PKEY_CTX *ctx, unsigned char *rout, size_t *routlen,
                            const unsigned char *sig, size_t siglen) {
    if (!ctx || ctx->op != OP_VERIFY_RECOVER || !ctx->pkey || !ctx->pkey->n || !ctx->pkey->e || !routlen) return 0;
    const size_t size = static_cast<size_t>(BN_num_bytes(ctx->pkey->n));
    if (!rout) {
        *routlen = size;
        return 1;
    }
    if (!sig) return 0;

    std::vector<unsigned char> em;
    if (!rsaPublicRaw(ctx->pkey, sig, siglen, em)) return 0;

    if (ctx->padding == RSA_NO_PADDING) {
        if (*routlen < size) {
            wowee_shim_log("EVP_PKEY_verify_recover: output buffer too small (%zu < %zu)", *routlen, size);
            return 0;
        }
        std::memcpy(rout, em.data(), size);
        *routlen = size;
        return 1;
    }

    /* RSA_PKCS1_PADDING, block type 1: 00 || 01 || FF..FF (>= 8) || 00 || D */
    if (size < RSA_PKCS1_PADDING_SIZE || em[0] != 0x00 || em[1] != 0x01) {
        wowee_shim_log("EVP_PKEY_verify_recover: bad PKCS#1 block type");
        return 0;
    }
    size_t i = 2;
    while (i < size && em[i] == 0xff) ++i;
    if (i >= size || em[i] != 0x00 || i - 2 < 8) {
        wowee_shim_log("EVP_PKEY_verify_recover: bad PKCS#1 padding");
        return 0;
    }
    ++i;
    const size_t dlen = size - i;
    if (*routlen < dlen) {
        wowee_shim_log("EVP_PKEY_verify_recover: output buffer too small (%zu < %zu)", *routlen, dlen);
        return 0;
    }
    std::memcpy(rout, em.data() + i, dlen);
    *routlen = dlen;
    return 1;
}

int EVP_PKEY_encrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
                     const unsigned char *in, size_t inlen) {
    if (!ctx || ctx->op != OP_ENCRYPT || !ctx->pkey || !ctx->pkey->n || !ctx->pkey->e || !outlen) return 0;
    const size_t size = static_cast<size_t>(BN_num_bytes(ctx->pkey->n));
    if (!out) {
        *outlen = size;
        return 1;
    }
    if (ctx->padding != RSA_NO_PADDING) {
        wowee_shim_log("EVP_PKEY_encrypt: only RSA_NO_PADDING is implemented in the shim");
        return 0;
    }
    if (*outlen < size) return 0;
    std::vector<unsigned char> em;
    if (!rsaPublicRaw(ctx->pkey, in, inlen, em)) return 0;
    std::memcpy(out, em.data(), size);
    *outlen = size;
    return 1;
}

WOWEE_SHIM_END
