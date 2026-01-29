#pragma once

#include <vector>
#include <cstdint>
#include <array>

namespace brightchain {

/**
 * AES-256-GCM encryption service.
 * Provides authenticated encryption with associated data.
 */
class AesGcm {
public:
    static constexpr size_t KEY_SIZE = 32;  // 256 bits
    static constexpr size_t IV_SIZE = 12;   // 96 bits (recommended for GCM)
    static constexpr size_t TAG_SIZE = 16;  // 128 bits

    using Key = std::array<uint8_t, KEY_SIZE>;
    using IV = std::array<uint8_t, IV_SIZE>;
    using Tag = std::array<uint8_t, TAG_SIZE>;

    /**
     * Generate a random encryption key.
     */
    static Key generateKey();

    /**
     * Generate a random IV.
     */
    static IV generateIV();

    /**
     * Encrypt data with AES-256-GCM.
     * @param plaintext Data to encrypt
     * @param key Encryption key
     * @param iv Initialization vector
     * @param tag Output authentication tag
     * @param aad Optional additional authenticated data
     * @return Encrypted data
     */
    static std::vector<uint8_t> encrypt(
        const std::vector<uint8_t>& plaintext,
        const Key& key,
        const IV& iv,
        Tag& tag,
        const std::vector<uint8_t>& aad = {}
    );

    /**
     * Decrypt data with AES-256-GCM.
     * @param ciphertext Encrypted data
     * @param key Decryption key
     * @param iv Initialization vector
     * @param tag Authentication tag
     * @param aad Optional additional authenticated data (must match what was used during encryption)
     * @return Decrypted data
     * @throws std::runtime_error if authentication fails
     */
    static std::vector<uint8_t> decrypt(
        const std::vector<uint8_t>& ciphertext,
        const Key& key,
        const IV& iv,
        const Tag& tag,
        const std::vector<uint8_t>& aad = {}
    );
};

} // namespace brightchain
