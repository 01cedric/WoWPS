#pragma once

#include <cstdint>
#include <vector>

namespace wowee {
namespace auth {

/**
 * Vanilla/TBC WoW Header Cipher
 *
 * Used for encrypting/decrypting World of Warcraft packet headers with the
 * legacy XOR+addition chaining cipher. Vanilla/classic uses the raw 40-byte
 * SRP session key; CMaNGOS TBC uses the same cipher with a 20-byte
 * HMAC-derived key.
 *
 * Algorithm (encrypt):
 *   encrypted = (plaintext ^ key[index]) + previousEncrypted
 *   index = (index + 1) % keyLen
 *
 * Algorithm (decrypt):
 *   plaintext = (encrypted - previousEncrypted) ^ key[index]
 *   index = (index + 1) % keyLen
 */
class VanillaCrypt {
public:
    VanillaCrypt() = default;
    ~VanillaCrypt() = default;

    /**
     * Initialize the cipher with the packet-header key.
     */
    void init(const std::vector<uint8_t>& key);

    /**
     * Encrypt outgoing header bytes (CMSG: 6 bytes)
     * @param data Pointer to header data to encrypt in-place
     * @param length Number of bytes to encrypt
     */
    void encrypt(uint8_t* data, size_t length);

    /**
     * Decrypt incoming header bytes (SMSG: 4 bytes)
     * @param data Pointer to header data to decrypt in-place
     * @param length Number of bytes to decrypt
     */
    void decrypt(uint8_t* data, size_t length);

private:
    std::vector<uint8_t> key_;
    uint8_t sendIndex_ = 0;
    uint8_t sendPrev_ = 0;
    uint8_t recvIndex_ = 0;
    uint8_t recvPrev_ = 0;
};

} // namespace auth
} // namespace wowee
