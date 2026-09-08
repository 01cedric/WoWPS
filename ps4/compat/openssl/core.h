/* WoWee PS4 crypto shim - subset of <openssl/core.h> (OSSL_PARAM, OSSL_LIB_CTX). */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_CORE_H
#define WOWEE_PS4_COMPAT_OPENSSL_CORE_H

#include "openssl/wowee_shim_linkage.h"

WOWEE_SHIM_BEGIN

typedef struct ossl_lib_ctx_st OSSL_LIB_CTX;

typedef struct ossl_param_st {
    const char *key;          /* the name of the parameter */
    unsigned int data_type;   /* OSSL_PARAM_* type id */
    void *data;               /* value being passed in or out */
    size_t data_size;         /* data size in bytes */
    size_t return_size;       /* returned size */
} OSSL_PARAM;

#define OSSL_PARAM_INTEGER          1
#define OSSL_PARAM_UNSIGNED_INTEGER 2
#define OSSL_PARAM_REAL             3
#define OSSL_PARAM_UTF8_STRING      4
#define OSSL_PARAM_OCTET_STRING     5
#define OSSL_PARAM_UTF8_PTR         6
#define OSSL_PARAM_OCTET_PTR        7

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_CORE_H */
