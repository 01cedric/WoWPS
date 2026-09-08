/* WoWee PS4 crypto shim - SHA-1 (FIPS 180-4). */
#include "openssl/sha.h"
#include <cstring>

namespace {

inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void sha1_transform(uint32_t h[5], const unsigned char block[64]) {
    uint32_t w[80];
    for (int t = 0; t < 16; ++t) {
        w[t] = (static_cast<uint32_t>(block[t * 4]) << 24) | (static_cast<uint32_t>(block[t * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[t * 4 + 2]) << 8) | static_cast<uint32_t>(block[t * 4 + 3]);
    }
    for (int t = 16; t < 80; ++t) w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int t = 0; t < 80; ++t) {
        uint32_t f, k;
        if (t < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (t < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (t < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        uint32_t temp = rotl(a, 5) + f + e + k + w[t];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = temp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

} // namespace

WOWEE_SHIM_BEGIN

int SHA1_Init(SHA_CTX *c) {
    if (!c) return 0;
    c->h[0] = 0x67452301u;
    c->h[1] = 0xEFCDAB89u;
    c->h[2] = 0x98BADCFEu;
    c->h[3] = 0x10325476u;
    c->h[4] = 0xC3D2E1F0u;
    c->total_len = 0;
    c->block_len = 0;
    return 1;
}

int SHA1_Update(SHA_CTX *c, const void *data, size_t len) {
    if (!c) return 0;
    if (len == 0) return 1;
    if (!data) return 0;
    const unsigned char *p = static_cast<const unsigned char *>(data);
    c->total_len += len;
    if (c->block_len) {
        size_t take = 64 - c->block_len;
        if (take > len) take = len;
        std::memcpy(c->block + c->block_len, p, take);
        c->block_len += static_cast<unsigned int>(take);
        p += take;
        len -= take;
        if (c->block_len == 64) {
            sha1_transform(c->h, c->block);
            c->block_len = 0;
        }
    }
    while (len >= 64) {
        sha1_transform(c->h, p);
        p += 64;
        len -= 64;
    }
    if (len) {
        std::memcpy(c->block, p, len);
        c->block_len = static_cast<unsigned int>(len);
    }
    return 1;
}

int SHA1_Final(unsigned char *md, SHA_CTX *c) {
    if (!c || !md) return 0;
    const uint64_t bits = c->total_len * 8;
    unsigned char pad = 0x80;
    SHA1_Update(c, &pad, 1);
    unsigned char zero = 0;
    while (c->block_len != 56) SHA1_Update(c, &zero, 1);
    unsigned char lenbuf[8];
    for (int i = 0; i < 8; ++i) lenbuf[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
    SHA1_Update(c, lenbuf, 8);
    for (int i = 0; i < 5; ++i) {
        md[i * 4] = static_cast<unsigned char>(c->h[i] >> 24);
        md[i * 4 + 1] = static_cast<unsigned char>(c->h[i] >> 16);
        md[i * 4 + 2] = static_cast<unsigned char>(c->h[i] >> 8);
        md[i * 4 + 3] = static_cast<unsigned char>(c->h[i]);
    }
    std::memset(c, 0, sizeof(*c));
    return 1;
}

unsigned char *SHA1(const unsigned char *d, size_t n, unsigned char *md) {
    static unsigned char m[SHA_DIGEST_LENGTH];
    if (!md) md = m;
    SHA_CTX c;
    SHA1_Init(&c);
    SHA1_Update(&c, d, n);
    SHA1_Final(md, &c);
    return md;
}

WOWEE_SHIM_END
