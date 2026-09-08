/*
 * WoWee PS4 crypto shim - subset of <openssl/evp.h>.
 *
 * Message digests (EVP_sha1 / EVP_md5 / EVP_sha256, EVP_Digest, EVP_MD_CTX)
 * and the OpenSSL-3 style RSA public-key path used by the Warden module
 * verifier: EVP_PKEY_fromdata with n/e parameters, then
 * EVP_PKEY_verify_recover (RSA "public decrypt") with RSA_NO_PADDING or
 * RSA_PKCS1_PADDING.
 */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_EVP_H
#define WOWEE_PS4_COMPAT_OPENSSL_EVP_H

#include "openssl/wowee_shim_linkage.h"
#include "openssl/crypto.h"
#include "openssl/core.h"
#include "openssl/bn.h"

#define EVP_MAX_MD_SIZE    64
#define EVP_MAX_BLOCK_LENGTH 128

/* EVP_PKEY_fromdata selections */
#define EVP_PKEY_KEY_PARAMETERS 0x84
#define EVP_PKEY_PUBLIC_KEY     0x87
#define EVP_PKEY_PRIVATE_KEY    0x8f
#define EVP_PKEY_KEYPAIR        0x8f

#define EVP_PKEY_NONE 0
#define EVP_PKEY_RSA  6

WOWEE_SHIM_BEGIN

typedef struct evp_md_st EVP_MD;
typedef struct evp_md_ctx_st EVP_MD_CTX;
typedef struct engine_st ENGINE;
typedef struct evp_pkey_st EVP_PKEY;
typedef struct evp_pkey_ctx_st EVP_PKEY_CTX;

/* ---- Digests ---------------------------------------------------------- */
const EVP_MD *EVP_sha1(void);
const EVP_MD *EVP_sha256(void);
const EVP_MD *EVP_md5(void);
const EVP_MD *EVP_get_digestbyname(const char *name);

int EVP_MD_get_size(const EVP_MD *md);
int EVP_MD_get_block_size(const EVP_MD *md);
#define EVP_MD_size(md)       EVP_MD_get_size(md)
#define EVP_MD_block_size(md) EVP_MD_get_block_size(md)

EVP_MD_CTX *EVP_MD_CTX_new(void);
void        EVP_MD_CTX_free(EVP_MD_CTX *ctx);
int         EVP_MD_CTX_reset(EVP_MD_CTX *ctx);
int         EVP_MD_CTX_copy_ex(EVP_MD_CTX *out, const EVP_MD_CTX *in);
const EVP_MD *EVP_MD_CTX_get0_md(const EVP_MD_CTX *ctx);
int EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *type, ENGINE *impl);
int EVP_DigestInit(EVP_MD_CTX *ctx, const EVP_MD *type);
int EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *d, size_t cnt);
int EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s);
int EVP_DigestFinal(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *s);
int EVP_Digest(const void *data, size_t count, unsigned char *md,
               unsigned int *size, const EVP_MD *type, ENGINE *impl);

/* ---- RSA public keys via OSSL_PARAM ------------------------------------ */
EVP_PKEY_CTX *EVP_PKEY_CTX_new_from_name(OSSL_LIB_CTX *libctx, const char *name,
                                         const char *propquery);
EVP_PKEY_CTX *EVP_PKEY_CTX_new(EVP_PKEY *pkey, ENGINE *e);
EVP_PKEY_CTX *EVP_PKEY_CTX_new_id(int id, ENGINE *e);
void          EVP_PKEY_CTX_free(EVP_PKEY_CTX *ctx);

int EVP_PKEY_fromdata_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_fromdata(EVP_PKEY_CTX *ctx, EVP_PKEY **ppkey, int selection,
                      OSSL_PARAM params[]);

void EVP_PKEY_free(EVP_PKEY *pkey);
int  EVP_PKEY_up_ref(EVP_PKEY *pkey);
int  EVP_PKEY_get_bits(const EVP_PKEY *pkey);
int  EVP_PKEY_get_size(const EVP_PKEY *pkey);
int  EVP_PKEY_get_id(const EVP_PKEY *pkey);
int  EVP_PKEY_get_base_id(const EVP_PKEY *pkey);
#define EVP_PKEY_bits(k)    EVP_PKEY_get_bits(k)
#define EVP_PKEY_size(k)    EVP_PKEY_get_size(k)
#define EVP_PKEY_id(k)      EVP_PKEY_get_id(k)
#define EVP_PKEY_base_id(k) EVP_PKEY_get_base_id(k)
/* Copies of the key's n / e (caller frees). NULL if absent. */
int EVP_PKEY_get_bn_param(const EVP_PKEY *pkey, const char *key_name, BIGNUM **bn);

int EVP_PKEY_verify_recover_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_verify_recover(EVP_PKEY_CTX *ctx, unsigned char *rout, size_t *routlen,
                            const unsigned char *sig, size_t siglen);
int EVP_PKEY_encrypt_init(EVP_PKEY_CTX *ctx);
int EVP_PKEY_encrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
                     const unsigned char *in, size_t inlen);
int EVP_PKEY_CTX_set_rsa_padding(EVP_PKEY_CTX *ctx, int pad_mode);
int EVP_PKEY_CTX_get_rsa_padding(EVP_PKEY_CTX *ctx, int *pad_mode);

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_EVP_H */
