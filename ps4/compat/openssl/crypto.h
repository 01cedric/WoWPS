/* WoWee PS4 crypto shim - subset of <openssl/crypto.h>. */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_CRYPTO_H
#define WOWEE_PS4_COMPAT_OPENSSL_CRYPTO_H

#include "openssl/wowee_shim_linkage.h"

/* Looks like an OpenSSL 3.0.x to code that checks the version macro. */
#define OPENSSL_VERSION_NUMBER 0x30000000L
#define OPENSSL_VERSION_TEXT   "WoWee PS4 crypto shim (OpenSSL 3.0 API subset)"

WOWEE_SHIM_BEGIN

void *OPENSSL_malloc(size_t num);
void *OPENSSL_zalloc(size_t num);
char *OPENSSL_strdup(const char *str);
void  OPENSSL_free(void *ptr);
void  OPENSSL_clear_free(void *ptr, size_t num);
void  OPENSSL_cleanse(void *ptr, size_t len);

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_CRYPTO_H */
