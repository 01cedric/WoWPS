/* WoWee PS4 crypto shim - MD5 (RFC 1321). */
#include "openssl/md5.h"
#include <cstring>

namespace {

inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

const uint32_t K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};

const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

void md5_transform(uint32_t h[4], const unsigned char block[64]) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = static_cast<uint32_t>(block[i * 4]) | (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 16) | (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rotl(a + f + K[i] + m[g], S[i]);
        a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}

} // namespace

WOWEE_SHIM_BEGIN

int MD5_Init(MD5_CTX *c) {
    if (!c) return 0;
    c->h[0] = 0x67452301u;
    c->h[1] = 0xefcdab89u;
    c->h[2] = 0x98badcfeu;
    c->h[3] = 0x10325476u;
    c->total_len = 0;
    c->block_len = 0;
    return 1;
}

int MD5_Update(MD5_CTX *c, const void *data, size_t len) {
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
            md5_transform(c->h, c->block);
            c->block_len = 0;
        }
    }
    while (len >= 64) {
        md5_transform(c->h, p);
        p += 64;
        len -= 64;
    }
    if (len) {
        std::memcpy(c->block, p, len);
        c->block_len = static_cast<unsigned int>(len);
    }
    return 1;
}

int MD5_Final(unsigned char *md, MD5_CTX *c) {
    if (!c || !md) return 0;
    const uint64_t bits = c->total_len * 8;
    unsigned char pad = 0x80;
    MD5_Update(c, &pad, 1);
    unsigned char zero = 0;
    while (c->block_len != 56) MD5_Update(c, &zero, 1);
    unsigned char lenbuf[8];
    for (int i = 0; i < 8; ++i) lenbuf[i] = static_cast<unsigned char>(bits >> (8 * i));
    MD5_Update(c, lenbuf, 8);
    for (int i = 0; i < 4; ++i) {
        md[i * 4] = static_cast<unsigned char>(c->h[i]);
        md[i * 4 + 1] = static_cast<unsigned char>(c->h[i] >> 8);
        md[i * 4 + 2] = static_cast<unsigned char>(c->h[i] >> 16);
        md[i * 4 + 3] = static_cast<unsigned char>(c->h[i] >> 24);
    }
    std::memset(c, 0, sizeof(*c));
    return 1;
}

unsigned char *MD5(const unsigned char *d, size_t n, unsigned char *md) {
    static unsigned char m[MD5_DIGEST_LENGTH];
    if (!md) md = m;
    MD5_CTX c;
    MD5_Init(&c);
    MD5_Update(&c, d, n);
    MD5_Final(md, &c);
    return md;
}

WOWEE_SHIM_END
