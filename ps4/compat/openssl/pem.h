/*
 * WoWee PS4 crypto shim - placeholder for <openssl/pem.h>.
 * WoWee includes this header from warden_module.cpp but does not call any
 * PEM function; nothing PEM-related is provided.
 */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_PEM_H
#define WOWEE_PS4_COMPAT_OPENSSL_PEM_H

#include "openssl/evp.h"
#include "openssl/rsa.h"

#endif /* WOWEE_PS4_COMPAT_OPENSSL_PEM_H */
