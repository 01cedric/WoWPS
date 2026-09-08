/* WoWee PS4 crypto shim - subset of <openssl/param_build.h>. */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_PARAM_BUILD_H
#define WOWEE_PS4_COMPAT_OPENSSL_PARAM_BUILD_H

#include "openssl/core.h"
#include "openssl/params.h"
#include "openssl/bn.h"

WOWEE_SHIM_BEGIN

typedef struct ossl_param_bld_st OSSL_PARAM_BLD;

OSSL_PARAM_BLD *OSSL_PARAM_BLD_new(void);
void            OSSL_PARAM_BLD_free(OSSL_PARAM_BLD *bld);
int             OSSL_PARAM_BLD_push_BN(OSSL_PARAM_BLD *bld, const char *key, const BIGNUM *bn);
int             OSSL_PARAM_BLD_push_uint(OSSL_PARAM_BLD *bld, const char *key, unsigned int val);
OSSL_PARAM     *OSSL_PARAM_BLD_to_param(OSSL_PARAM_BLD *bld);

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_PARAM_BUILD_H */
