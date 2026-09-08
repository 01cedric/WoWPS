/* WoWee PS4 crypto shim - subset of <openssl/rand.h>. */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_RAND_H
#define WOWEE_PS4_COMPAT_OPENSSL_RAND_H

#include "openssl/wowee_shim_linkage.h"

WOWEE_SHIM_BEGIN

/*
 * Fills buf with num cryptographically random bytes.  Returns 1 on success.
 * Sources, in order: PS4 libSceRandom (loaded at runtime, no link dependency),
 * getrandom(2)/getentropy(3) where available, /dev/urandom.  If none of them
 * work the buffer is still filled from a SHA-256 based fallback generator
 * seeded from clocks/addresses, a warning is logged once, and 0 is returned.
 */
int RAND_bytes(unsigned char *buf, int num);
int RAND_priv_bytes(unsigned char *buf, int num);
int RAND_status(void);
int RAND_poll(void);

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_RAND_H */
