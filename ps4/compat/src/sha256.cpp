/* WoWee PS4 crypto shim - SHA-256 (FIPS 180-4). */
#include "openssl/sha.h"
#include <cstring>

namespace {

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

void sha256_transform(uint32_t h[8], const unsigned char block[64]) {
    uint32_t w[64];
    for (int t = 0; t < 16; ++t) {
        w[t] = (static_cast<uint32_t>(block[t * 4]) << 24) | (static_cast<uint32_t>(block[t * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[t * 4 + 2]) << 8) | static_cast<uint32_t>(block[t * 4 + 3]);
    }
    for (int t = 16; t < 64; ++t) {
        uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
        uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; ++t) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + K[t] + w[t];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

} // namespace

WOWEE_SHIM_BEGIN

int SHA256_Init(SHA256_CTX *c) {
    if (!c) return 0;
    static const uint32_t H0[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::memcpy(c->h, H0, sizeof H0);
    c->total_len = 0;
    c->block_len = 0;
    return 1;
}

int SHA256_Update(SHA256_CTX *c, const void *data, size_t len) {
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
            sha256_transform(c->h, c->block);
            c->block_len = 0;
        }
    }
    while (len >= 64) {
        sha256_transform(c->h, p);
        p += 64;
        len -= 64;
    }
    if (len) {
        std::memcpy(c->block, p, len);
        c->block_len = static_cast<unsigned int>(len);
    }
    return 1;
}

int SHA256_Final(unsigned char *md, SHA256_CTX *c) {
    if (!c || !md) return 0;
    const uint64_t bits = c->total_len * 8;
    unsigned char pad = 0x80;
    SHA256_Update(c, &pad, 1);
    unsigned char zero = 0;
    while (c->block_len != 56) SHA256_Update(c, &zero, 1);
    unsigned char lenbuf[8];
    for (int i = 0; i < 8; ++i) lenbuf[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
    SHA256_Update(c, lenbuf, 8);
    for (int i = 0; i < 8; ++i) {
        md[i * 4] = static_cast<unsigned char>(c->h[i] >> 24);
        md[i * 4 + 1] = static_cast<unsigned char>(c->h[i] >> 16);
        md[i * 4 + 2] = static_cast<unsigned char>(c->h[i] >> 8);
        md[i * 4 + 3] = static_cast<unsigned char>(c->h[i]);
    }
    std::memset(c, 0, sizeof(*c));
    return 1;
}

unsigned char *SHA256(const unsigned char *d, size_t n, unsigned char *md) {
    static unsigned char m[SHA256_DIGEST_LENGTH];
    if (!md) md = m;
    SHA256_CTX c;
    SHA256_Init(&c);
    SHA256_Update(&c, d, n);
    SHA256_Final(md, &c);
    return md;
}

WOWEE_SHIM_END
