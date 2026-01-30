#pragma once

#include <brightchain/ec_key_pair.hpp>
#include <brightchain/paillier.hpp>
#include <array>
#include <string>
#include <vector>
#include <memory>
#include <ctime>

namespace brightchain {

/**
 * Member ID is a 16-byte GUID
 */
using MemberId = std::array<uint8_t, 16>;

enum class MemberType : uint8_t {
    Admin = 0,
    System = 1,
    User = 2,
    Anonymous = 3
};

/**
 * Represents a member with cryptographic capabilities.
 * Compatible with TypeScript Member from @digitaldefiance/ecies-lib.
 * Uses BIP44 key derivation path: m/44'/0'/0'/0/0
 */
class Member {
public:
    /**
     * Generate a new member with random keys.
     */
    static Member generate(
        MemberType type,
        const std::string& name,
        const std::string& email
    );

    /**
     * Generate a new BIP39 mnemonic (12 words).
     */
    static std::string generateMnemonic();

    /**
     * Validate a BIP39 mnemonic.
     */
    static bool validateMnemonic(const std::string& mnemonic);

    /**
     * Create member from BIP39 mnemonic.
     * Uses derivation path m/44'/0'/0'/0/0
     */
    static Member fromMnemonic(
        const std::string& mnemonic,
        MemberType type,
        const std::string& name,
        const std::string& email
    );

    /**
     * Create member from existing keys.
     */
    static Member fromKeys(
        MemberType type,
        const std::string& name,
        const std::string& email,
        const std::vector<uint8_t>& publicKey,
        const std::vector<uint8_t>& privateKey
    );

    /**
     * Create member from public key only (no signing capability).
     */
    static Member fromPublicKey(
        MemberType type,
        const std::string& name,
        const std::string& email,
        const std::vector<uint8_t>& publicKey
    );

    // Accessors
    const MemberId& id() const { return id_; }
    std::vector<uint8_t> idBytes() const;
    std::string idHex() const;
    
    MemberType type() const { return type_; }
    const std::string& name() const { return name_; }
    const std::string& email() const { return email_; }
    
    std::vector<uint8_t> publicKey() const { return publicKey_; }
    std::vector<uint8_t> privateKey() const;
    bool hasPrivateKey() const { return keyPair_ != nullptr; }
    
    std::time_t dateCreated() const { return dateCreated_; }
    std::time_t dateUpdated() const { return dateUpdated_; }
    
    // Voting keys
    std::shared_ptr<PaillierPublicKey> votingPublicKey() const { return votingPublicKey_; }
    std::shared_ptr<PaillierPrivateKey> votingPrivateKey() const { return votingPrivateKey_; }
    bool hasVotingKeys() const { return votingPublicKey_ != nullptr; }
    bool hasVotingPrivateKey() const { return votingPrivateKey_ != nullptr; }
    
    /**
     * Derive Paillier voting keys from ECDH keys.
     * @throws std::runtime_error if no private key loaded
     */
    void deriveVotingKeys(int keypairBitLength = 3072, int primeTestIterations = 256);
    
    /**
     * Load pre-generated voting keys.
     */
    void loadVotingKeys(std::shared_ptr<PaillierPublicKey> publicKey,
                       std::shared_ptr<PaillierPrivateKey> privateKey = nullptr);
    
    /**
     * Unload voting private key from memory.
     */
    void unloadVotingPrivateKey();

    /**
     * Sign data with private key.
     * @throws std::runtime_error if no private key loaded
     */
    std::vector<uint8_t> sign(const std::vector<uint8_t>& data) const;

    /**
     * Verify signature with this member's public key.
     */
    bool verify(
        const std::vector<uint8_t>& data,
        const std::vector<uint8_t>& signature
    ) const;

    /**
     * Verify signature with any public key.
     */
    static bool verifySignature(
        const std::vector<uint8_t>& data,
        const std::vector<uint8_t>& signature,
        const std::vector<uint8_t>& publicKey
    );

    /**
     * Serialize to JSON (public data only).
     */
    std::string toJson(bool includePrivateData = false) const;

    /**
     * Deserialize from JSON.
     */
    static Member fromJson(const std::string& json);

    // Copy/move support
    Member(const Member& other);
    Member& operator=(const Member& other);
    Member(Member&& other) noexcept = default;
    Member& operator=(Member&& other) noexcept = default;

private:
    Member(
        MemberType type,
        const std::string& name,
        const std::string& email,
        const std::vector<uint8_t>& publicKey,
        std::unique_ptr<EcKeyPair> keyPair = nullptr
    );

    static MemberId generateId();
    static std::vector<uint8_t> deriveKeyFromMnemonic(const std::string& mnemonic);

    MemberId id_;
    MemberType type_;
    std::string name_;
    std::string email_;
    std::vector<uint8_t> publicKey_;
    std::unique_ptr<EcKeyPair> keyPair_;
    std::time_t dateCreated_;
    std::time_t dateUpdated_;
    
    // Voting keys for homomorphic encryption
    std::shared_ptr<PaillierPublicKey> votingPublicKey_;
    std::shared_ptr<PaillierPrivateKey> votingPrivateKey_;
};

} // namespace brightchain
