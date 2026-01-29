#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace brightchain {

// Forward declarations
class PaillierPublicKey;
class PaillierPrivateKey;

/**
 * Paillier public key for homomorphic encryption
 */
class PaillierPublicKey {
public:
    PaillierPublicKey(const std::vector<uint8_t>& n, const std::vector<uint8_t>& g);
    ~PaillierPublicKey() = default;

    // Encrypt plaintext
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext) const;
    
    // Homomorphic addition of ciphertexts
    std::vector<uint8_t> addition(const std::vector<std::vector<uint8_t>>& ciphertexts) const;
    
    // Pseudo-homomorphic addition of plaintext to ciphertext
    std::vector<uint8_t> plaintextAddition(const std::vector<uint8_t>& ciphertext,
                                           const std::vector<std::vector<uint8_t>>& plaintexts) const;
    
    // Pseudo-homomorphic multiplication
    std::vector<uint8_t> multiply(const std::vector<uint8_t>& ciphertext, int k) const;
    
    // Get bit length of modulus
    int bitLength() const;
    
    // Get modulus n
    const std::vector<uint8_t>& n() const { return n_; }
    
    // Get generator g
    const std::vector<uint8_t>& g() const { return g_; }
    
    // Get n as hex string
    std::string nHex() const;
    
    // Get g as hex string
    std::string gHex() const;
    
    // Convert bigint to hex string
    static std::string bigintToHex(const std::vector<uint8_t>& bigint);
    
    // Get n^2 (for internal use)
    const std::vector<uint8_t>& n2() const { return n2_; }
    
    // JSON serialization
    std::string toJson() const;
    static std::shared_ptr<PaillierPublicKey> fromJson(const std::string& json);

private:
    std::vector<uint8_t> n_;      // Public modulus
    std::vector<uint8_t> g_;      // Generator
    std::vector<uint8_t> n2_;     // n^2 cached
};

/**
 * Paillier private key for decryption
 */
class PaillierPrivateKey {
public:
    PaillierPrivateKey(const std::vector<uint8_t>& lambda,
                       const std::vector<uint8_t>& mu,
                       std::shared_ptr<PaillierPublicKey> publicKey,
                       const std::vector<uint8_t>& p = {},
                       const std::vector<uint8_t>& q = {});
    ~PaillierPrivateKey() = default;

    // Decrypt ciphertext
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext) const;
    
    // Get random factor used in encryption (requires p and q)
    std::vector<uint8_t> getRandomFactor(const std::vector<uint8_t>& ciphertext) const;
    
    // Get public key
    std::shared_ptr<PaillierPublicKey> publicKey() const { return publicKey_; }
    
    // Get bit length
    int bitLength() const { return publicKey_->bitLength(); }
    
    // Check if p and q are available
    bool hasPrimes() const { return !p_.empty() && !q_.empty(); }
    
    // Accessors
    const std::vector<uint8_t>& lambda() const { return lambda_; }
    const std::vector<uint8_t>& mu() const { return mu_; }
    std::string lambdaHex() const;
    std::string muHex() const;
    
    // JSON serialization
    std::string toJson() const;
    static std::shared_ptr<PaillierPrivateKey> fromJson(const std::string& json);

private:
    std::vector<uint8_t> lambda_;
    std::vector<uint8_t> mu_;
    std::vector<uint8_t> p_;  // Prime p (optional)
    std::vector<uint8_t> q_;  // Prime q (optional)
    std::shared_ptr<PaillierPublicKey> publicKey_;
};

/**
 * Paillier key pair
 */
struct PaillierKeyPair {
    std::shared_ptr<PaillierPublicKey> publicKey;
    std::shared_ptr<PaillierPrivateKey> privateKey;
};

/**
 * Derive Paillier voting keys from ECDH keys
 */
PaillierKeyPair deriveVotingKeysFromECDH(
    const std::vector<uint8_t>& ecdhPrivateKey,
    const std::vector<uint8_t>& ecdhPublicKey,
    int keypairBitLength = 3072,
    int primeTestIterations = 256
);

} // namespace brightchain
