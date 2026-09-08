/* WoWee PS4 crypto shim - subset of <openssl/hmac.h>. */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_HMAC_H
#define WOWEE_PS4_COMPAT_OPENSSL_HMAC_H

#include "openssl/wowee_shim_linkage.h"
#include "openssl/evp.h"

#define HMAC_MAX_MD_CBLOCK_SIZE 144

WOWEE_SHIM_BEGIN

typedef struct hmac_ctx_st HMAC_CTX;

/* One-shot HMAC.  Returns md (or a static buffer if md is NULL) or NULL on error. */
unsigned char *HMAC(const EVP_MD *evp_md, const void *key, int key_len,
                    const unsigned char *data, size_t data_len,
                    unsigned char *md, unsigned int *md_len);

HMAC_CTX *HMAC_CTX_new(void);
void      HMAC_CTX_free(HMAC_CTX *ctx);
int       HMAC_CTX_reset(HMAC_CTX *ctx);
int  HMAC_Init_ex(HMAC_CTX *ctx, const void *key, int len, const EVP_MD *md, ENGINE *impl);
int  HMAC_Update(HMAC_CTX *ctx, const unsigned char *data, size_t len);
int  HMAC_Final(HMAC_CTX *ctx, unsigned char *md, unsigned int *len);
size_t HMAC_size(const HMAC_CTX *e);

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_HMAC_H */
