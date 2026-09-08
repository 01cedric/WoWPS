/* WoWee PS4 crypto shim - subset of <openssl/rsa.h> (padding constants only). */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_RSA_H
#define WOWEE_PS4_COMPAT_OPENSSL_RSA_H

#include "openssl/wowee_shim_linkage.h"
#include "openssl/bn.h"

#define RSA_PKCS1_PADDING      1
#define RSA_NO_PADDING         3
#define RSA_PKCS1_OAEP_PADDING 4
#define RSA_X931_PADDING       5
#define RSA_PKCS1_PSS_PADDING  6

#define RSA_PKCS1_PADDING_SIZE 11

#define RSA_F4 0x10001L
#define RSA_3  0x3L

#endif /* WOWEE_PS4_COMPAT_OPENSSL_RSA_H */
