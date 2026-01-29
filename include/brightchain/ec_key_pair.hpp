#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <memory>

typedef struct ec_key_st EC_KEY;

namespace brightchain {

/**
 * Elliptic curve key pair using secp256k1.
 */
class EcKeyPair {
public:
    /**
     * Generate a new key pair.
     */
    static EcKeyPair generate();

    /**
     * Load from private key bytes.
     */
    static EcKeyPair fromPrivateKey(const std::vector<uint8_t>& privateKey);

    /**
     * Load from hex-encoded private key.
     */
    static EcKeyPair fromPrivateKeyHex(const std::string& privateKeyHex);

    ~EcKeyPair();
    EcKeyPair(const EcKeyPair&) = delete;
    EcKeyPair& operator=(const EcKeyPair&) = delete;
    EcKeyPair(EcKeyPair&& other) noexcept;
    EcKeyPair& operator=(EcKeyPair&& other) noexcept;

    /**
     * Get public key in compressed format (33 bytes).
     */
    std::vector<uint8_t> publicKey() const;

    /**
     * Get private key (32 bytes).
     */
    std::vector<uint8_t> privateKey() const;

    /**
     * Get public key as hex string.
     */
    std::string publicKeyHex() const;

    /**
     * Get private key as hex string.
     */
    std::string privateKeyHex() const;

    /**
     * Sign data with private key.
     */
    std::vector<uint8_t> sign(const std::vector<uint8_t>& data) const;

    /**
     * Verify signature with public key.
     */
    static bool verify(
        const std::vector<uint8_t>& data,
        const std::vector<uint8_t>& signature,
        const std::vector<uint8_t>& publicKey
    );

private:
    explicit EcKeyPair(EC_KEY* key);
    EC_KEY* key_;
};

} // namespace brightchain
