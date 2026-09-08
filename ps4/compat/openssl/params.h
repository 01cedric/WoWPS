/* WoWee PS4 crypto shim - subset of <openssl/params.h>. */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_PARAMS_H
#define WOWEE_PS4_COMPAT_OPENSSL_PARAMS_H

#include "openssl/core.h"
#include "openssl/bn.h"

WOWEE_SHIM_BEGIN

/* Frees an array produced by OSSL_PARAM_BLD_to_param (and the data it owns). */
void OSSL_PARAM_free(OSSL_PARAM *params);
/* Finds the parameter named key (NULL if absent or params NULL). */
OSSL_PARAM *OSSL_PARAM_locate(OSSL_PARAM *params, const char *key);
const OSSL_PARAM *OSSL_PARAM_locate_const(const OSSL_PARAM *params, const char *key);
/* Reads an unsigned-integer parameter into *val (allocated if *val is NULL). */
int OSSL_PARAM_get_BN(const OSSL_PARAM *p, BIGNUM **val);

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_PARAMS_H */
