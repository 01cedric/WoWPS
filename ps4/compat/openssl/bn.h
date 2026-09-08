/*
 * WoWee PS4 crypto shim - subset of <openssl/bn.h>.
 *
 * Arbitrary precision signed integers with the OpenSSL BIGNUM calling
 * conventions (1 = success, 0 = failure; output may alias inputs).
 */
#ifndef WOWEE_PS4_COMPAT_OPENSSL_BN_H
#define WOWEE_PS4_COMPAT_OPENSSL_BN_H

#include "openssl/wowee_shim_linkage.h"
#include "openssl/crypto.h"

/* x86_64 (Linux and the PS4's FreeBSD-derived ABI) is LP64: BN_ULONG is 64 bit. */
typedef unsigned long BN_ULONG;
#define BN_BYTES 8
#define BN_BITS2 64

WOWEE_SHIM_BEGIN

typedef struct bignum_st BIGNUM;
typedef struct bignum_ctx BN_CTX;

/* Lifetime */
BIGNUM *BN_new(void);
BIGNUM *BN_secure_new(void);
void    BN_free(BIGNUM *a);
void    BN_clear_free(BIGNUM *a);
void    BN_clear(BIGNUM *a);
BIGNUM *BN_dup(const BIGNUM *a);
BIGNUM *BN_copy(BIGNUM *to, const BIGNUM *from);

BN_CTX *BN_CTX_new(void);
BN_CTX *BN_CTX_secure_new(void);
void    BN_CTX_free(BN_CTX *ctx);
void    BN_CTX_start(BN_CTX *ctx);
void    BN_CTX_end(BN_CTX *ctx);
BIGNUM *BN_CTX_get(BN_CTX *ctx);

/* Conversion */
BIGNUM *BN_bin2bn(const unsigned char *s, int len, BIGNUM *ret);
int     BN_bn2bin(const BIGNUM *a, unsigned char *to);
int     BN_bn2binpad(const BIGNUM *a, unsigned char *to, int tolen);
BIGNUM *BN_lebin2bn(const unsigned char *s, int len, BIGNUM *ret);
int     BN_bn2lebinpad(const BIGNUM *a, unsigned char *to, int tolen);
int     BN_hex2bn(BIGNUM **a, const char *str);
char   *BN_bn2hex(const BIGNUM *a);
int     BN_dec2bn(BIGNUM **a, const char *str);
char   *BN_bn2dec(const BIGNUM *a);

/* Words and predicates */
int      BN_set_word(BIGNUM *a, BN_ULONG w);
BN_ULONG BN_get_word(const BIGNUM *a);
int      BN_zero(BIGNUM *a);
int      BN_one(BIGNUM *a);
const BIGNUM *BN_value_one(void);
int      BN_is_zero(const BIGNUM *a);
int      BN_is_one(const BIGNUM *a);
int      BN_is_odd(const BIGNUM *a);
int      BN_is_negative(const BIGNUM *a);
int      BN_is_word(const BIGNUM *a, BN_ULONG w);
int      BN_abs_is_word(const BIGNUM *a, BN_ULONG w);
void     BN_set_negative(BIGNUM *a, int n);
int      BN_num_bits(const BIGNUM *a);
int      BN_num_bytes(const BIGNUM *a);
int      BN_is_bit_set(const BIGNUM *a, int n);

/* Comparison */
int BN_cmp(const BIGNUM *a, const BIGNUM *b);
int BN_ucmp(const BIGNUM *a, const BIGNUM *b);

/* Arithmetic */
int BN_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
int BN_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
int BN_uadd(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
int BN_usub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b);
int BN_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx);
int BN_sqr(BIGNUM *r, const BIGNUM *a, BN_CTX *ctx);
int BN_div(BIGNUM *dv, BIGNUM *rem, const BIGNUM *a, const BIGNUM *d, BN_CTX *ctx);
int BN_mod(BIGNUM *rem, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx);
int BN_nnmod(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx);
int BN_mod_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx);
int BN_mod_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx);
int BN_mod_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx);
int BN_mod_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p, const BIGNUM *m, BN_CTX *ctx);
int BN_lshift(BIGNUM *r, const BIGNUM *a, int n);
int BN_rshift(BIGNUM *r, const BIGNUM *a, int n);
int BN_add_word(BIGNUM *a, BN_ULONG w);
int BN_sub_word(BIGNUM *a, BN_ULONG w);
int BN_mul_word(BIGNUM *a, BN_ULONG w);
BN_ULONG BN_mod_word(const BIGNUM *a, BN_ULONG w);

/* Random (RAND_bytes based) */
int BN_rand(BIGNUM *rnd, int bits, int top, int bottom);
int BN_rand_range(BIGNUM *rnd, const BIGNUM *range);

#define BN_RAND_TOP_ANY    (-1)
#define BN_RAND_TOP_ONE    0
#define BN_RAND_TOP_TWO    1
#define BN_RAND_BOTTOM_ANY 0
#define BN_RAND_BOTTOM_ODD 1

WOWEE_SHIM_END

#endif /* WOWEE_PS4_COMPAT_OPENSSL_BN_H */
