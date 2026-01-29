#pragma once

#include "brightchain/ec_key_pair.hpp"
#include "brightchain/aes_gcm.hpp"
#include <vector>
#include <cstdint>

namespace brightchain {

/**
 * ECIES encryption types matching TypeScript implementation.
 */
enum class EciesEncryptionType : uint8_t {
    Basic = 33,       // No length prefix
    WithLength = 66,  // Includes 8-byte length prefix
    Multiple = 99     // Multiple recipients (not implemented yet)
};

/**
 * ECIES (Elliptic Curve Integrated Encryption Scheme) implementation.
 * Uses secp256k1 + AES-256-GCM.
 * Single recipient formats:
 *   Basic: version(1) + cipherSuite(1) + type(1) + ephemeralPubKey(33) + IV(12) + ciphertext + authTag(16)
 *   WithLength: Basic format + length(8) before ciphertext
 * Multiple recipient format (type 99):
 *   version(1) + cipherSuite(1) + type(1) + ephemeralPubKey(33) + IV(12) + 
 *   recipientCount(4) + [pubKeyIndex(4) + encryptedSymmetricKey(65)]... + ciphertext + authTag(16)
 */
class Ecies {
public:
    /**
     * Encrypt data for a recipient's public key (Basic mode).
     * @param plaintext Data to encrypt
     * @param recipientPublicKey Recipient's public key (compressed, 33 bytes)
     * @return Encrypted data
     */
    static std::vector<uint8_t> encryptBasic(
        const std::vector<uint8_t>& plaintext,
        const std::vector<uint8_t>& recipientPublicKey
    );

    /**
     * Encrypt data for a recipient's public key (WithLength mode).
     * @param plaintext Data to encrypt
     * @param recipientPublicKey Recipient's public key (compressed, 33 bytes)
     * @return Encrypted data with length prefix
     */
    static std::vector<uint8_t> encryptWithLength(
        const std::vector<uint8_t>& plaintext,
        const std::vector<uint8_t>& recipientPublicKey
    );

    /**
     * Encrypt data for multiple recipients (Multiple mode, type 99).
     * Each recipient can decrypt with their own private key.
     * @param plaintext Data to encrypt
     * @param recipientPublicKeys Vector of recipient public keys (each 33 bytes compressed)
     * @return Encrypted data with all recipient key material
     */
    static std::vector<uint8_t> encryptMultiple(
        const std::vector<uint8_t>& plaintext,
        const std::vector<std::vector<uint8_t>>& recipientPublicKeys
    );

    /**
     * Decrypt data with private key.
     * @param ciphertext Encrypted data (any mode)
     * @param keyPair Recipient's key pair
     * @return Decrypted data
     */
    static std::vector<uint8_t> decrypt(
        const std::vector<uint8_t>& ciphertext,
        const EcKeyPair& keyPair
    );

private:
    static std::vector<uint8_t> encryptInternal(
        const std::vector<uint8_t>& plaintext,
        const std::vector<uint8_t>& recipientPublicKey,
        EciesEncryptionType type
    );

    /**
     * Encrypt the symmetric key for a specific recipient using ECIES.
     * @param symmetricKey The symmetric key to encrypt (32 bytes)
     * @param recipientPublicKey The recipient's public key (33 bytes)
     * @param ephemeralPrivateKey The ephemeral private key to use (32 bytes)
     * @param ephemeralPublicKey The ephemeral public key (33 bytes)
     * @param iv The IV to use (12 bytes)
     * @return Encrypted symmetric key (~65 bytes with secp256k1)
     */
    static std::vector<uint8_t> encryptSymmetricKey(
        const AesGcm::Key& symmetricKey,
        const std::vector<uint8_t>& recipientPublicKey,
        const std::vector<uint8_t>& ephemeralPrivateKey,
        const std::vector<uint8_t>& ephemeralPublicKey,
        const AesGcm::IV& iv
    );

    /**
     * Decrypt the symmetric key using ECDH and derive from ephemeral key.
     * @param encryptedSymmetricKey The encrypted key material
     * @param ephemeralPublicKey The ephemeral public key
     * @param privateKey The recipient's private key
     * @param iv The IV used
     * @return Decrypted symmetric key (32 bytes)
     */
    static AesGcm::Key decryptSymmetricKey(
        const std::vector<uint8_t>& encryptedSymmetricKey,
        const std::vector<uint8_t>& ephemeralPublicKey,
        const std::vector<uint8_t>& privateKey,
        const AesGcm::IV& iv
    );
};

} // namespace brightchain
