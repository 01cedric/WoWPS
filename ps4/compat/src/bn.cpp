/*
 * WoWee PS4 crypto shim - BIGNUM implementation.
 *
 * Signed arbitrary-precision integers stored as normalized little-endian
 * base-2^32 limbs.  Schoolbook multiplication, Knuth algorithm D division and
 * left-to-right square-and-multiply modular exponentiation.  Semantics follow
 * OpenSSL: BN_mod's remainder takes the sign of the dividend, BN_mod_exp
 * returns a value in [0, |m|), BN_bn2hex prints whole bytes in upper case.
 */
#include "shim_internal.hpp"
#include "openssl/rand.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

WOWEE_SHIM_BEGIN

void wowee_shim_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::fputs("[ps4-crypto-shim] ", stderr);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    va_end(ap);
}

/* ---- OPENSSL_* allocation helpers (declared in openssl/crypto.h) --------- */

void *OPENSSL_malloc(size_t num) { return std::malloc(num ? num : 1); }
void *OPENSSL_zalloc(size_t num) {
    void *p = OPENSSL_malloc(num);
    if (p) std::memset(p, 0, num);
    return p;
}
char *OPENSSL_strdup(const char *str) {
    if (!str) return nullptr;
    size_t n = std::strlen(str) + 1;
    char *p = static_cast<char *>(OPENSSL_malloc(n));
    if (p) std::memcpy(p, str, n);
    return p;
}
void OPENSSL_free(void *ptr) { std::free(ptr); }
void OPENSSL_cleanse(void *ptr, size_t len) {
    if (!ptr) return;
    volatile unsigned char *p = static_cast<volatile unsigned char *>(ptr);
    while (len--) *p++ = 0;
}
void OPENSSL_clear_free(void *ptr, size_t num) {
    if (!ptr) return;
    OPENSSL_cleanse(ptr, num);
    std::free(ptr);
}

WOWEE_SHIM_END

/* ======================================================================== */
/* Magnitude arithmetic on limb vectors                                      */
/* ======================================================================== */
namespace {

using Limbs = std::vector<uint32_t>;

inline void normalize(Limbs &v) {
    while (!v.empty() && v.back() == 0) v.pop_back();
}

inline int cmp_mag(const Limbs &a, const Limbs &b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

Limbs add_mag(const Limbs &a, const Limbs &b) {
    const Limbs &x = a.size() >= b.size() ? a : b;
    const Limbs &y = a.size() >= b.size() ? b : a;
    Limbs r(x.size() + 1);
    uint64_t carry = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        uint64_t s = static_cast<uint64_t>(x[i]) + (i < y.size() ? y[i] : 0u) + carry;
        r[i] = static_cast<uint32_t>(s);
        carry = s >> 32;
    }
    r[x.size()] = static_cast<uint32_t>(carry);
    normalize(r);
    return r;
}

/* requires |a| >= |b| */
Limbs sub_mag(const Limbs &a, const Limbs &b) {
    Limbs r(a.size());
    int64_t borrow = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int64_t s = static_cast<int64_t>(a[i]) - (i < b.size() ? b[i] : 0u) - borrow;
        if (s < 0) {
            s += static_cast<int64_t>(1) << 32;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r[i] = static_cast<uint32_t>(s);
    }
    normalize(r);
    return r;
}

Limbs mul_mag(const Limbs &a, const Limbs &b) {
    if (a.empty() || b.empty()) return Limbs();
    Limbs r(a.size() + b.size(), 0);
    for (size_t i = 0; i < a.size(); ++i) {
        uint64_t carry = 0;
        const uint64_t ai = a[i];
        if (ai == 0) continue;
        for (size_t j = 0; j < b.size(); ++j) {
            uint64_t t = ai * b[j] + r[i + j] + carry;
            r[i + j] = static_cast<uint32_t>(t);
            carry = t >> 32;
        }
        size_t k = i + b.size();
        while (carry) {
            uint64_t t = static_cast<uint64_t>(r[k]) + carry;
            r[k] = static_cast<uint32_t>(t);
            carry = t >> 32;
            ++k;
        }
    }
    normalize(r);
    return r;
}

Limbs sqr_mag(const Limbs &a) { return mul_mag(a, a); }

inline int clz32(uint32_t x) {
    if (x == 0) return 32;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clz(x);
#else
    int n = 0;
    while (!(x & 0x80000000u)) { x <<= 1; ++n; }
    return n;
#endif
}

Limbs shl_mag(const Limbs &a, unsigned bits) {
    if (a.empty()) return Limbs();
    const unsigned limbShift = bits / 32, bitShift = bits % 32;
    Limbs r(a.size() + limbShift + 1, 0);
    for (size_t i = 0; i < a.size(); ++i) {
        uint64_t v = static_cast<uint64_t>(a[i]) << bitShift;
        r[i + limbShift] |= static_cast<uint32_t>(v);
        r[i + limbShift + 1] |= static_cast<uint32_t>(v >> 32);
    }
    normalize(r);
    return r;
}

Limbs shr_mag(const Limbs &a, unsigned bits) {
    const unsigned limbShift = bits / 32, bitShift = bits % 32;
    if (limbShift >= a.size()) return Limbs();
    Limbs r(a.size() - limbShift, 0);
    for (size_t i = 0; i < r.size(); ++i) {
        uint64_t v = a[i + limbShift];
        if (i + limbShift + 1 < a.size()) v |= static_cast<uint64_t>(a[i + limbShift + 1]) << 32;
        r[i] = static_cast<uint32_t>(v >> bitShift);
    }
    normalize(r);
    return r;
}

/* Division by a single limb; returns the remainder. */
uint32_t divmod_word(const Limbs &u, uint32_t v, Limbs &q) {
    q.assign(u.size(), 0);
    uint64_t rem = 0;
    for (size_t i = u.size(); i-- > 0;) {
        uint64_t cur = (rem << 32) | u[i];
        q[i] = static_cast<uint32_t>(cur / v);
        rem = cur % v;
    }
    normalize(q);
    return static_cast<uint32_t>(rem);
}

/* Knuth, TAOCP vol. 2, algorithm D (base 2^32).  v must be non-zero. */
void divmod_mag(const Limbs &u, const Limbs &v, Limbs &q, Limbs &r) {
    if (cmp_mag(u, v) < 0) {
        q.clear();
        r = u;
        return;
    }
    if (v.size() == 1) {
        uint32_t rem = divmod_word(u, v[0], q);
        r.clear();
        if (rem) r.push_back(rem);
        return;
    }

    const size_t n = v.size();
    const size_t m = u.size() - n;
    const unsigned s = static_cast<unsigned>(clz32(v.back()));

    Limbs vn(n), un(u.size() + 1);
    for (size_t i = n - 1; i > 0; --i)
        vn[i] = (v[i] << s) | (s ? static_cast<uint32_t>(static_cast<uint64_t>(v[i - 1]) >> (32 - s)) : 0u);
    vn[0] = v[0] << s;
    un[u.size()] = s ? static_cast<uint32_t>(static_cast<uint64_t>(u[u.size() - 1]) >> (32 - s)) : 0u;
    for (size_t i = u.size() - 1; i > 0; --i)
        un[i] = (u[i] << s) | (s ? static_cast<uint32_t>(static_cast<uint64_t>(u[i - 1]) >> (32 - s)) : 0u);
    un[0] = u[0] << s;

    q.assign(m + 1, 0);
    const uint64_t base = static_cast<uint64_t>(1) << 32;

    for (size_t j = m + 1; j-- > 0;) {
        uint64_t num = (static_cast<uint64_t>(un[j + n]) << 32) | un[j + n - 1];
        uint64_t qhat = num / vn[n - 1];
        uint64_t rhat = num % vn[n - 1];
        while (qhat >= base || qhat * vn[n - 2] > ((rhat << 32) | un[j + n - 2])) {
            --qhat;
            rhat += vn[n - 1];
            if (rhat >= base) break;
        }

        /* Multiply and subtract. */
        int64_t borrow = 0;
        uint64_t carry = 0;
        for (size_t i = 0; i < n; ++i) {
            uint64_t p = qhat * vn[i] + carry;
            carry = p >> 32;
            int64_t t = static_cast<int64_t>(un[i + j]) - static_cast<int64_t>(static_cast<uint32_t>(p)) - borrow;
            if (t < 0) {
                t += static_cast<int64_t>(base);
                borrow = 1;
            } else {
                borrow = 0;
            }
            un[i + j] = static_cast<uint32_t>(t);
        }
        int64_t t = static_cast<int64_t>(un[j + n]) - static_cast<int64_t>(carry) - borrow;
        un[j + n] = static_cast<uint32_t>(t);
        q[j] = static_cast<uint32_t>(qhat);

        if (t < 0) {
            /* qhat was one too large: add v back. */
            --q[j];
            uint64_t c = 0;
            for (size_t i = 0; i < n; ++i) {
                uint64_t sum = static_cast<uint64_t>(un[i + j]) + vn[i] + c;
                un[i + j] = static_cast<uint32_t>(sum);
                c = sum >> 32;
            }
            un[j + n] = static_cast<uint32_t>(static_cast<uint64_t>(un[j + n]) + c);
        }
    }
    normalize(q);

    /* Remainder: un[0..n) shifted back right by s. */
    r.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        uint64_t v64 = un[i];
        if (i + 1 < un.size()) v64 |= static_cast<uint64_t>(un[i + 1]) << 32;
        r[i] = static_cast<uint32_t>(v64 >> s);
    }
    normalize(r);
}

Limbs mod_mag(const Limbs &a, const Limbs &m) {
    Limbs q, r;
    divmod_mag(a, m, q, r);
    return r;
}

inline size_t bitlen_mag(const Limbs &a) {
    if (a.empty()) return 0;
    return (a.size() - 1) * 32 + static_cast<size_t>(32 - clz32(a.back()));
}

inline bool bit_mag(const Limbs &a, size_t n) {
    size_t limb = n / 32;
    if (limb >= a.size()) return false;
    return (a[limb] >> (n % 32)) & 1u;
}

/* a^e mod m with 0 <= a < m, m > 1.  Fixed 4-bit window, left to right. */
Limbs modexp_mag(const Limbs &a, const Limbs &e, const Limbs &m) {
    const size_t bits = bitlen_mag(e);
    Limbs one{1u};
    if (bits == 0) return mod_mag(one, m);
    if (a.empty()) return Limbs();

    /* Precompute a^0 .. a^15. */
    Limbs table[16];
    table[0] = mod_mag(one, m);
    table[1] = a;
    for (int i = 2; i < 16; ++i) table[i] = mod_mag(mul_mag(table[i - 1], a), m);

    Limbs result = table[0];
    size_t i = bits;
    /* Handle the leading bits that do not form a full window. */
    size_t lead = bits % 4;
    if (lead) {
        unsigned w = 0;
        while (lead--) {
            --i;
            w = (w << 1) | (bit_mag(e, i) ? 1u : 0u);
        }
        result = mod_mag(mul_mag(result, table[w]), m);
    }
    while (i >= 4) {
        i -= 4;
        for (int k = 0; k < 4; ++k) result = mod_mag(sqr_mag(result), m);
        unsigned w = 0;
        for (int k = 3; k >= 0; --k) w = (w << 1) | (bit_mag(e, i + static_cast<size_t>(k)) ? 1u : 0u);
        if (w) result = mod_mag(mul_mag(result, table[w]), m);
    }
    return result;
}

inline bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
inline unsigned hex_val(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    return static_cast<unsigned>(c - 'A' + 10);
}

} // namespace

/* ======================================================================== */
/* Public BIGNUM API                                                         */
/* ======================================================================== */
WOWEE_SHIM_BEGIN

static inline void set_sign(BIGNUM *a, bool neg) { a->neg = neg && !a->d.empty(); }

BIGNUM *BN_new(void) {
    try {
        return new bignum_st();
    } catch (...) {
        return nullptr;
    }
}
BIGNUM *BN_secure_new(void) { return BN_new(); }

void BN_free(BIGNUM *a) { delete a; }
void BN_clear_free(BIGNUM *a) {
    if (!a) return;
    if (!a->d.empty()) OPENSSL_cleanse(a->d.data(), a->d.size() * sizeof(uint32_t));
    delete a;
}
void BN_clear(BIGNUM *a) {
    if (!a) return;
    if (!a->d.empty()) OPENSSL_cleanse(a->d.data(), a->d.size() * sizeof(uint32_t));
    a->d.clear();
    a->neg = false;
}

BIGNUM *BN_dup(const BIGNUM *a) {
    if (!a) return nullptr;
    BIGNUM *r = BN_new();
    if (!r) return nullptr;
    r->d = a->d;
    r->neg = a->neg;
    return r;
}

BIGNUM *BN_copy(BIGNUM *to, const BIGNUM *from) {
    if (!to || !from) return nullptr;
    if (to != from) {
        to->d = from->d;
        to->neg = from->neg;
    }
    return to;
}

BN_CTX *BN_CTX_new(void) {
    try {
        return new bignum_ctx();
    } catch (...) {
        return nullptr;
    }
}
BN_CTX *BN_CTX_secure_new(void) { return BN_CTX_new(); }
void BN_CTX_free(BN_CTX *ctx) {
    if (!ctx) return;
    for (BIGNUM *b : ctx->pool) BN_free(b);
    delete ctx;
}
void BN_CTX_start(BN_CTX *ctx) {
    if (ctx) ++ctx->depth;
}
void BN_CTX_end(BN_CTX *ctx) {
    if (ctx && ctx->depth > 0) --ctx->depth;
}
BIGNUM *BN_CTX_get(BN_CTX *ctx) {
    if (!ctx) return nullptr;
    BIGNUM *b = BN_new();
    if (b) ctx->pool.push_back(b);
    return b;
}

/* ---- Conversion --------------------------------------------------------- */

BIGNUM *BN_bin2bn(const unsigned char *s, int len, BIGNUM *ret) {
    BIGNUM *r = ret ? ret : BN_new();
    if (!r) return nullptr;
    r->d.clear();
    r->neg = false;
    if (len < 0) len = 0;
    if (s && len > 0) {
        r->d.assign((static_cast<size_t>(len) + 3) / 4, 0);
        for (int i = 0; i < len; ++i) {
            size_t byteFromEnd = static_cast<size_t>(len - 1 - i);
            r->d[byteFromEnd / 4] |= static_cast<uint32_t>(s[i]) << (8 * (byteFromEnd % 4));
        }
        normalize(r->d);
    }
    return r;
}

int BN_num_bits(const BIGNUM *a) {
    if (!a) return 0;
    return static_cast<int>(bitlen_mag(a->d));
}

int BN_num_bytes(const BIGNUM *a) { return (BN_num_bits(a) + 7) / 8; }

int BN_bn2bin(const BIGNUM *a, unsigned char *to) {
    if (!a) return 0;
    const int n = BN_num_bytes(a);
    for (int i = 0; i < n; ++i) {
        size_t byteFromEnd = static_cast<size_t>(n - 1 - i);
        to[i] = static_cast<unsigned char>(a->d[byteFromEnd / 4] >> (8 * (byteFromEnd % 4)));
    }
    return n;
}

int BN_bn2binpad(const BIGNUM *a, unsigned char *to, int tolen) {
    if (!a || tolen < 0) return -1;
    const int n = BN_num_bytes(a);
    if (n > tolen) return -1;
    std::memset(to, 0, static_cast<size_t>(tolen - n));
    BN_bn2bin(a, to + (tolen - n));
    return tolen;
}

BIGNUM *BN_lebin2bn(const unsigned char *s, int len, BIGNUM *ret) {
    if (len < 0) len = 0;
    std::vector<unsigned char> tmp(s, s + len);
    std::reverse(tmp.begin(), tmp.end());
    return BN_bin2bn(tmp.data(), len, ret);
}

int BN_bn2lebinpad(const BIGNUM *a, unsigned char *to, int tolen) {
    int r = BN_bn2binpad(a, to, tolen);
    if (r < 0) return r;
    std::reverse(to, to + tolen);
    return r;
}

int BN_hex2bn(BIGNUM **bn, const char *str) {
    if (!str || *str == '\0') return 0;
    const char *a = str;
    bool neg = false;
    if (*a == '-') {
        neg = true;
        ++a;
    }
    size_t i = 0;
    while (is_hex(a[i])) ++i;
    /* No hex digits at all ("", "-", "zz"): OpenSSL returns 0 and leaves *bn untouched. */
    if (i == 0) return 0;
    const int num = static_cast<int>(i) + (neg ? 1 : 0);
    if (!bn) return num;

    BIGNUM *ret = *bn ? *bn : BN_new();
    if (!ret) return 0;
    ret->d.assign((i + 7) / 8, 0);
    for (size_t k = 0; k < i; ++k) {
        size_t nibbleFromEnd = i - 1 - k;
        ret->d[nibbleFromEnd / 8] |= hex_val(a[k]) << (4 * (nibbleFromEnd % 8));
    }
    normalize(ret->d);
    set_sign(ret, neg);
    *bn = ret;
    return num;
}

char *BN_bn2hex(const BIGNUM *a) {
    if (!a) return nullptr;
    if (a->d.empty()) return OPENSSL_strdup("0");
    static const char Hex[] = "0123456789ABCDEF";
    const int n = BN_num_bytes(a);
    char *buf = static_cast<char *>(OPENSSL_malloc(static_cast<size_t>(n) * 2 + 2));
    if (!buf) return nullptr;
    char *p = buf;
    if (a->neg) *p++ = '-';
    for (int i = 0; i < n; ++i) {
        size_t byteFromEnd = static_cast<size_t>(n - 1 - i);
        unsigned v = (a->d[byteFromEnd / 4] >> (8 * (byteFromEnd % 4))) & 0xffu;
        *p++ = Hex[v >> 4];
        *p++ = Hex[v & 0xf];
    }
    *p = '\0';
    return buf;
}

int BN_dec2bn(BIGNUM **bn, const char *str) {
    if (!str || *str == '\0') return 0;
    const char *a = str;
    bool neg = false;
    if (*a == '-') {
        neg = true;
        ++a;
    }
    size_t i = 0;
    while (a[i] >= '0' && a[i] <= '9') ++i;
    const int num = static_cast<int>(i) + (neg ? 1 : 0);
    if (!bn) return num;
    BIGNUM *ret = *bn ? *bn : BN_new();
    if (!ret) return 0;
    ret->d.clear();
    ret->neg = false;
    for (size_t k = 0; k < i; ++k) {
        BN_mul_word(ret, 10);
        BN_add_word(ret, static_cast<BN_ULONG>(a[k] - '0'));
    }
    set_sign(ret, neg);
    *bn = ret;
    return num;
}

char *BN_bn2dec(const BIGNUM *a) {
    if (!a) return nullptr;
    if (a->d.empty()) return OPENSSL_strdup("0");
    std::string digits;
    Limbs cur = a->d, q;
    while (!cur.empty()) {
        uint32_t rem = divmod_word(cur, 1000000000u, q);
        cur.swap(q);
        char chunk[16];
        if (cur.empty())
            std::snprintf(chunk, sizeof chunk, "%u", rem);
        else
            std::snprintf(chunk, sizeof chunk, "%09u", rem);
        digits.insert(0, chunk);
    }
    if (a->neg) digits.insert(0, "-");
    return OPENSSL_strdup(digits.c_str());
}

/* ---- Words and predicates ---------------------------------------------- */

int BN_set_word(BIGNUM *a, BN_ULONG w) {
    if (!a) return 0;
    a->d.clear();
    a->neg = false;
    uint64_t v = w;
    while (v) {
        a->d.push_back(static_cast<uint32_t>(v));
        v >>= 32;
    }
    return 1;
}

BN_ULONG BN_get_word(const BIGNUM *a) {
    if (!a) return 0;
    if (a->d.size() > 2) return ~static_cast<BN_ULONG>(0);
    uint64_t v = 0;
    for (size_t i = a->d.size(); i-- > 0;) v = (v << 32) | a->d[i];
    return static_cast<BN_ULONG>(v);
}

int BN_zero(BIGNUM *a) {
    if (!a) return 0;
    a->d.clear();
    a->neg = false;
    return 1;
}
int BN_one(BIGNUM *a) { return BN_set_word(a, 1); }

const BIGNUM *BN_value_one(void) {
    static const bignum_st one{Limbs{1u}, false};
    return &one;
}

int BN_is_zero(const BIGNUM *a) { return a && a->d.empty(); }
int BN_is_one(const BIGNUM *a) { return a && !a->neg && a->d.size() == 1 && a->d[0] == 1; }
int BN_is_odd(const BIGNUM *a) { return a && !a->d.empty() && (a->d[0] & 1u); }
int BN_is_negative(const BIGNUM *a) { return a && a->neg; }
int BN_abs_is_word(const BIGNUM *a, BN_ULONG w) {
    if (!a) return 0;
    bignum_st t;
    BN_set_word(&t, w);
    return cmp_mag(a->d, t.d) == 0;
}
int BN_is_word(const BIGNUM *a, BN_ULONG w) { return BN_abs_is_word(a, w) && !a->neg; }
void BN_set_negative(BIGNUM *a, int n) {
    if (a) set_sign(a, n != 0);
}
int BN_is_bit_set(const BIGNUM *a, int n) {
    if (!a || n < 0) return 0;
    return bit_mag(a->d, static_cast<size_t>(n)) ? 1 : 0;
}

/* ---- Comparison --------------------------------------------------------- */

int BN_ucmp(const BIGNUM *a, const BIGNUM *b) { return cmp_mag(a->d, b->d); }

int BN_cmp(const BIGNUM *a, const BIGNUM *b) {
    if (!a || !b) {
        if (!a && !b) return 0;
        return a ? 1 : -1;
    }
    if (a->neg != b->neg) return a->neg ? -1 : 1;
    int c = cmp_mag(a->d, b->d);
    return a->neg ? -c : c;
}

/* ---- Arithmetic --------------------------------------------------------- */

int BN_uadd(BIGNUM *r, const BIGNUM *a, const BIGNUM *b) {
    r->d = add_mag(a->d, b->d);
    r->neg = false;
    return 1;
}

int BN_usub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b) {
    if (cmp_mag(a->d, b->d) < 0) return 0;
    r->d = sub_mag(a->d, b->d);
    r->neg = false;
    return 1;
}

int BN_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b) {
    if (!r || !a || !b) return 0;
    if (a->neg == b->neg) {
        Limbs s = add_mag(a->d, b->d);
        bool neg = a->neg;
        r->d = std::move(s);
        set_sign(r, neg);
        return 1;
    }
    int c = cmp_mag(a->d, b->d);
    if (c == 0) {
        BN_zero(r);
        return 1;
    }
    if (c > 0) {
        bool neg = a->neg;
        r->d = sub_mag(a->d, b->d);
        set_sign(r, neg);
    } else {
        bool neg = b->neg;
        r->d = sub_mag(b->d, a->d);
        set_sign(r, neg);
    }
    return 1;
}

int BN_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b) {
    if (!r || !a || !b) return 0;
    bignum_st nb;
    nb.d = b->d;
    nb.neg = !b->neg && !b->d.empty();
    return BN_add(r, a, &nb);
}

int BN_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *) {
    if (!r || !a || !b) return 0;
    bool neg = a->neg != b->neg;
    r->d = mul_mag(a->d, b->d);
    set_sign(r, neg);
    return 1;
}

int BN_sqr(BIGNUM *r, const BIGNUM *a, BN_CTX *) {
    if (!r || !a) return 0;
    r->d = sqr_mag(a->d);
    r->neg = false;
    return 1;
}

int BN_div(BIGNUM *dv, BIGNUM *rem, const BIGNUM *a, const BIGNUM *d, BN_CTX *) {
    if (!a || !d) return 0;
    if (d->d.empty()) {
        wowee_shim_log("BN_div: division by zero");
        return 0;
    }
    Limbs q, r;
    divmod_mag(a->d, d->d, q, r);
    /* Truncating division: remainder takes the dividend's sign, quotient sign is the XOR. */
    const bool qneg = a->neg != d->neg;
    const bool rneg = a->neg;
    if (dv) {
        dv->d = std::move(q);
        set_sign(dv, qneg);
    }
    if (rem) {
        rem->d = std::move(r);
        set_sign(rem, rneg);
    }
    return 1;
}

int BN_mod(BIGNUM *rem, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx) {
    return BN_div(nullptr, rem, a, m, ctx);
}

int BN_nnmod(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx) {
    if (!r || !a || !m) return 0;
    if (!BN_mod(r, a, m, ctx)) return 0;
    if (!r->neg) return 1;
    /* r = |m| - |r| */
    r->d = sub_mag(m->d, r->d);
    r->neg = false;
    return 1;
}

int BN_mod_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx) {
    bignum_st t;
    if (!BN_add(&t, a, b)) return 0;
    return BN_nnmod(r, &t, m, ctx);
}

int BN_mod_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx) {
    bignum_st t;
    if (!BN_sub(&t, a, b)) return 0;
    return BN_nnmod(r, &t, m, ctx);
}

int BN_mod_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m, BN_CTX *ctx) {
    bignum_st t;
    if (!BN_mul(&t, a, b, ctx)) return 0;
    return BN_nnmod(r, &t, m, ctx);
}

int BN_mod_exp(BIGNUM *r, const BIGNUM *a, const BIGNUM *p, const BIGNUM *m, BN_CTX *ctx) {
    if (!r || !a || !p || !m) return 0;
    if (m->d.empty()) {
        wowee_shim_log("BN_mod_exp: zero modulus");
        return 0;
    }
    /* x^p mod 1 (or -1) is zero for every p, including p == 0. */
    if (m->d.size() == 1 && m->d[0] == 1) return BN_zero(r);

    bignum_st base;
    if (!BN_nnmod(&base, a, m, ctx)) return 0;
    Limbs res = modexp_mag(base.d, p->d, m->d);
    r->d = std::move(res);
    r->neg = false;
    return 1;
}

int BN_lshift(BIGNUM *r, const BIGNUM *a, int n) {
    if (!r || !a || n < 0) return 0;
    bool neg = a->neg;
    r->d = shl_mag(a->d, static_cast<unsigned>(n));
    set_sign(r, neg);
    return 1;
}

int BN_rshift(BIGNUM *r, const BIGNUM *a, int n) {
    if (!r || !a || n < 0) return 0;
    bool neg = a->neg;
    r->d = shr_mag(a->d, static_cast<unsigned>(n));
    set_sign(r, neg);
    return 1;
}

int BN_add_word(BIGNUM *a, BN_ULONG w) {
    bignum_st t;
    BN_set_word(&t, w);
    return BN_add(a, a, &t);
}

int BN_sub_word(BIGNUM *a, BN_ULONG w) {
    bignum_st t;
    BN_set_word(&t, w);
    return BN_sub(a, a, &t);
}

int BN_mul_word(BIGNUM *a, BN_ULONG w) {
    bignum_st t;
    BN_set_word(&t, w);
    return BN_mul(a, a, &t, nullptr);
}

BN_ULONG BN_mod_word(const BIGNUM *a, BN_ULONG w) {
    if (!a || w == 0) return ~static_cast<BN_ULONG>(0);
    bignum_st t, r;
    BN_set_word(&t, w);
    if (!BN_mod(&r, a, &t, nullptr)) return ~static_cast<BN_ULONG>(0);
    return BN_get_word(&r);
}

/* ---- Random ------------------------------------------------------------- */

int BN_rand(BIGNUM *rnd, int bits, int top, int bottom) {
    if (!rnd || bits < 0) return 0;
    if (bits == 0) {
        if (top != BN_RAND_TOP_ANY || bottom != BN_RAND_BOTTOM_ANY) return 0;
        return BN_zero(rnd);
    }
    const int bytes = (bits + 7) / 8;
    std::vector<unsigned char> buf(static_cast<size_t>(bytes));
    if (RAND_bytes(buf.data(), bytes) != 1) return 0;
    const int extra = bytes * 8 - bits;
    buf[0] &= static_cast<unsigned char>(0xffu >> extra);
    const unsigned topBit = 1u << (7 - extra);
    if (top >= 0) {
        buf[0] |= static_cast<unsigned char>(topBit);
        if (top == BN_RAND_TOP_TWO) {
            if (bits == 1) return 0;
            if (topBit == 1) buf[1] |= 0x80u; else buf[0] |= static_cast<unsigned char>(topBit >> 1);
        }
    }
    if (bottom == BN_RAND_BOTTOM_ODD) buf[bytes - 1] |= 1u;
    BN_bin2bn(buf.data(), bytes, rnd);
    return 1;
}

int BN_rand_range(BIGNUM *rnd, const BIGNUM *range) {
    if (!rnd || !range || range->neg || range->d.empty()) return 0;
    const int bits = BN_num_bits(range);
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (!BN_rand(rnd, bits, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY)) return 0;
        if (BN_cmp(rnd, range) < 0) return 1;
    }
    return 0;
}

WOWEE_SHIM_END
