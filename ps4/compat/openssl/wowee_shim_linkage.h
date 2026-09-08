/*
 * WoWee PS4 OpenSSL-compatible crypto shim - linkage helpers.
 *
 * Every public function of the shim is declared between WOWEE_SHIM_BEGIN and
 * WOWEE_SHIM_END.  Normally that expands to `extern "C" { ... }` so the shim
 * exports the exact symbol names OpenSSL uses and the unchanged WoWee sources
 * link against it.  When WOWEE_CRYPTO_SHIM_NS is defined (only the host test
 * build does this) the whole API is placed in that C++ namespace instead, so
 * the shim and the real libcrypto can coexist in one test executable.
 */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_SHIM_LINKAGE_H
#define WOWEE_PS4_COMPAT_OPENSSL_SHIM_LINKAGE_H

#include <stddef.h>
#include <stdint.h>

#define WOWEE_CRYPTO_SHIM 1

#if defined(__cplusplus)
#  if defined(WOWEE_CRYPTO_SHIM_NS)
#    define WOWEE_SHIM_BEGIN namespace WOWEE_CRYPTO_SHIM_NS {
#    define WOWEE_SHIM_END }
#  else
#    define WOWEE_SHIM_BEGIN extern "C" {
#    define WOWEE_SHIM_END }
#  endif
#else
#  define WOWEE_SHIM_BEGIN
#  define WOWEE_SHIM_END
#endif

#endif /* WOWEE_PS4_COMPAT_OPENSSL_SHIM_LINKAGE_H */
