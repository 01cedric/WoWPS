#include "auth/big_num.hpp"
#include "core/logger.hpp"
#include <openssl/rand.h>
#include <algorithm>

namespace wowee {
namespace auth {

BigNum::BigNum() : bn(BN_new()) {
    if (!bn) {
        LOG_ERROR("Failed to create BIGNUM");
    }
}

BigNum::BigNum(uint32_t value) : bn(BN_new()) {
    BN_set_word(bn, value);
}

BigNum::BigNum(const std::vector<uint8_t>& bytes, bool littleEndian) : bn(BN_new()) {
    if (littleEndian) {
        // Convert little-endian to big-endian for OpenSSL
        std::vector<uint8_t> reversed = bytes;
        std::reverse(reversed.begin(), reversed.end());
        BN_bin2bn(reversed.data(), reversed.size(), bn);
    } else {
        BN_bin2bn(bytes.data(), bytes.size(), bn);
    }
}

BigNum::~BigNum() {
    if (bn) {
        BN_free(bn);
    }
}

BigNum::BigNum(const BigNum& other) : bn(BN_dup(other.bn)) {}

BigNum& BigNum::operator=(const BigNum& other) {
    if (this != &other) {
        BN_free(bn);
        bn = BN_dup(other.bn);
    }
    return *this;
}

BigNum::BigNum(BigNum&& other) noexcept : bn(other.bn) {
    other.bn = nullptr;
}

BigNum& BigNum::operator=(BigNum&& other) noexcept {
    if (this != &other) {
        BN_free(bn);
        bn = other.bn;
        other.bn = nullptr;
    }
    return *this;
}

BigNum BigNum::fromRandom(int bytes) {
    std::vector<uint8_t> randomBytes(bytes);
    RAND_bytes(randomBytes.data(), bytes);
    return BigNum(randomBytes, true);
}

BigNum BigNum::fromHex(const std::string& hex) {
    BigNum result;
    BN_hex2bn(&result.bn, hex.c_str());
    return result;
}
BigNum BigNum::add(const BigNum& other) const {
    BigNum result;
    BN_add(result.bn, bn, other.bn);
    return result;
}

BigNum BigNum::subtract(const BigNum& other) const {
    BigNum result;
    BN_sub(result.bn, bn, other.bn);
    return result;
}

BigNum BigNum::multiply(const BigNum& other) const {
    BigNum result;
    BN_CTX* ctx = BN_CTX_new();
    BN_mul(result.bn, bn, other.bn, ctx);
    BN_CTX_free(ctx);
    return result;
}

BigNum BigNum::mod(const BigNum& modulus) const {
    BigNum result;
    BN_CTX* ctx = BN_CTX_new();
    BN_mod(result.bn, bn, modulus.bn, ctx);
    BN_CTX_free(ctx);
    return result;
}

BigNum BigNum::modPow(const BigNum& exponent, const BigNum& modulus) const {
    BigNum result;
    BN_CTX* ctx = BN_CTX_new();
    BN_mod_exp(result.bn, bn, exponent.bn, modulus.bn, ctx);
    BN_CTX_free(ctx);
    return result;
}

bool BigNum::equals(const BigNum& other) const {
    return BN_cmp(bn, other.bn) == 0;
}

bool BigNum::isZero() const {
    return BN_is_zero(bn);
}

std::vector<uint8_t> BigNum::toArray(bool littleEndian, int minSize) const {
    int size = BN_num_bytes(bn);
    if (minSize > size) {
        size = minSize;
    }

    std::vector<uint8_t> bytes(size, 0);
    [[maybe_unused]] int actualSize = BN_bn2bin(bn, bytes.data() + (size - BN_num_bytes(bn)));

    if (littleEndian) {
        std::reverse(bytes.begin(), bytes.end());
    }

    return bytes;
}

std::string BigNum::toHex() const {
    char* hex = BN_bn2hex(bn);
    // BN_bn2hex returns nullptr on allocation failure
    if (!hex) return "(null)";
    std::string result(hex);
    OPENSSL_free(hex);
    return result;
}
} // namespace auth
} // namespace wowee
