/* WoWee PS4 crypto shim - subset of <openssl/sha.h> (SHA-1 and SHA-256). */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_SHA_H
#define WOWEE_PS4_COMPAT_OPENSSL_SHA_H

#include "openssl/wowee_shim_linkage.h"

#define SHA_DIGEST_LENGTH    20
#define SHA_LBLOCK           16
#define SHA_CBLOCK           64
#define SHA256_DIGEST_LENGTH 32
#define SHA256_CBLOCK        64

WOWEE_SHIM_BEGIN

typedef struct SHAstate_st {
    uint32_t h[5];
    uint64_t total_len;   /* bytes hashed so far */
    unsigned char block[64];
    unsigned int block_len;
} SHA_CTX;

typedef struct SHA256state_st {
    uint32_t h[8];
    uint64_t total_len;
    unsigned char block[64];
    unsigned int block_len;
} SHA256_CTX;

int SHA1_Init(SHA_CTX *c);
int SHA1_Update(SHA_CTX *c, const void *data, size_t len);
int SHA1_Final(unsigned char *md, SHA_CTX *c);
unsigned char *SHA1(const unsigned char *d, size_t n, unsigned char *md);

int SHA256_Init(SHA256_CTX *c);
int SHA256_Update(SHA256_CTX *c, const void *data, size_t len);
int SHA256_Final(unsigned char *md, SHA256_CTX *c);
unsigned char *SHA256(const unsigned char *d, size_t n, unsigned char *md);

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_SHA_H */
