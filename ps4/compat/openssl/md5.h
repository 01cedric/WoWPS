/* WoWee PS4 crypto shim - subset of <openssl/md5.h>. */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_MD5_H
#define WOWEE_PS4_COMPAT_OPENSSL_MD5_H

#include "openssl/wowee_shim_linkage.h"

#define MD5_DIGEST_LENGTH 16
#define MD5_LBLOCK        16
#define MD5_CBLOCK        64

WOWEE_SHIM_BEGIN

typedef struct MD5state_st {
    uint32_t h[4];
    uint64_t total_len;
    unsigned char block[64];
    unsigned int block_len;
} MD5_CTX;

int MD5_Init(MD5_CTX *c);
int MD5_Update(MD5_CTX *c, const void *data, size_t len);
int MD5_Final(unsigned char *md, MD5_CTX *c);
unsigned char *MD5(const unsigned char *d, size_t n, unsigned char *md);

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_MD5_H */
