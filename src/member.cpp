#include <brightchain/member.hpp>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cstring>

extern "C" {
#include "bip39.h"
#include "bip32.h"
#include "curves.h"
#include "sha2.h" // Use trezor-crypto's SHA2
}

namespace brightchain {

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16)));
    }
    return bytes;
}

Member::Member(
    MemberType type,
    const std::string& name,
    const std::string& email,
    const std::vector<uint8_t>& publicKey,
    std::unique_ptr<EcKeyPair> keyPair
) : type_(type),
    name_(name),
    email_(email),
    publicKey_(publicKey),
    keyPair_(std::move(keyPair)),
    dateCreated_(std::time(nullptr)),
    dateUpdated_(std::time(nullptr)) {
    
    if (publicKey.size() != 33) {
        throw std::invalid_argument("Public key must be 33 bytes (compressed)");
    }
    
    // Generate deterministic ID from public key using trezor-crypto SHA256
    uint8_t hash[32];
    sha256_Raw(publicKey.data(), publicKey.size(), hash);
    std::copy(hash, hash + 16, id_.begin());
}

MemberId Member::generateId() {
    MemberId id;
    if (RAND_bytes(id.data(), id.size()) != 1) {
        throw std::runtime_error("Failed to generate random ID");
    }
    return id;
}

// BIP32/BIP44 key derivation: m/44'/60'/0'/0/0
std::vector<uint8_t> Member::deriveKeyFromMnemonic(const std::string& mnemonic) {
    // Use trezor-crypto for proper BIP39 -> seed conversion
    uint8_t seed[64];
    mnemonic_to_seed(mnemonic.c_str(), "", seed, nullptr);
    
    // Proper BIP32/BIP44 derivation path m/44'/60'/0'/0/0
    HDNode node;
    hdnode_from_seed(seed, 64, SECP256K1_NAME, &node);
    
    // Derive path: m/44'/60'/0'/0/0
    hdnode_private_ckd_prime(&node, 44);  // m/44'
    hdnode_private_ckd_prime(&node, 60);  // m/44'/60'
    hdnode_private_ckd_prime(&node, 0);   // m/44'/60'/0'
    hdnode_private_ckd(&node, 0);         // m/44'/60'/0'/0
    hdnode_private_ckd(&node, 0);         // m/44'/60'/0'/0/0
    
    return std::vector<uint8_t>(node.private_key, node.private_key + 32);
}

std::string Member::generateMnemonic() {
    const char* mnemonic = mnemonic_generate(128); // 128 bits = 12 words
    if (!mnemonic) {
        throw std::runtime_error("Failed to generate mnemonic");
    }
    std::string result(mnemonic);
    mnemonic_clear();
    return result;
}

bool Member::validateMnemonic(const std::string& mnemonic) {
    return mnemonic_check(mnemonic.c_str()) == 1;
}

Member Member::generate(
    MemberType type,
    const std::string& name,
    const std::string& email
) {
    auto keyPair = std::make_unique<EcKeyPair>(EcKeyPair::generate());
    auto publicKey = keyPair->publicKey();
    
    return Member(type, name, email, publicKey, std::move(keyPair));
}

Member Member::fromMnemonic(
    const std::string& mnemonic,
    MemberType type,
    const std::string& name,
    const std::string& email
) {
    auto privateKey = deriveKeyFromMnemonic(mnemonic);
    auto keyPair = std::make_unique<EcKeyPair>(EcKeyPair::fromPrivateKey(privateKey));
    auto publicKey = keyPair->publicKey();
    
    return Member(type, name, email, publicKey, std::move(keyPair));
}

Member Member::fromKeys(
    MemberType type,
    const std::string& name,
    const std::string& email,
    const std::vector<uint8_t>& publicKey,
    const std::vector<uint8_t>& privateKey
) {
    auto keyPair = std::make_unique<EcKeyPair>(EcKeyPair::fromPrivateKey(privateKey));
    
    // Verify public key matches
    auto derivedPubKey = keyPair->publicKey();
    if (derivedPubKey != publicKey) {
        throw std::invalid_argument("Public key does not match private key");
    }
    
    return Member(type, name, email, publicKey, std::move(keyPair));
}

Member Member::fromPublicKey(
    MemberType type,
    const std::string& name,
    const std::string& email,
    const std::vector<uint8_t>& publicKey
) {
    return Member(type, name, email, publicKey, nullptr);
}

std::vector<uint8_t> Member::idBytes() const {
    return std::vector<uint8_t>(id_.begin(), id_.end());
}

std::string Member::idHex() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto byte : id_) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> Member::privateKey() const {
    if (!keyPair_) {
        throw std::runtime_error("No private key loaded");
    }
    return keyPair_->privateKey();
}

std::vector<uint8_t> Member::sign(const std::vector<uint8_t>& data) const {
    if (!keyPair_) {
        throw std::runtime_error("No private key loaded");
    }
    return keyPair_->sign(data);
}

bool Member::verify(
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& signature
) const {
    return EcKeyPair::verify(data, signature, publicKey_);
}

bool Member::verifySignature(
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& publicKey
) {
    return EcKeyPair::verify(data, signature, publicKey);
}

void Member::deriveVotingKeys(int keypairBitLength, int primeTestIterations) {
    if (!keyPair_) {
        throw std::runtime_error("No private key loaded");
    }
    
    auto keyPair = deriveVotingKeysFromECDH(
        keyPair_->privateKey(),
        publicKey_,
        keypairBitLength,
        primeTestIterations
    );
    
    votingPublicKey_ = keyPair.publicKey;
    votingPrivateKey_ = keyPair.privateKey;
}

void Member::loadVotingKeys(std::shared_ptr<PaillierPublicKey> publicKey,
                           std::shared_ptr<PaillierPrivateKey> privateKey) {
    votingPublicKey_ = publicKey;
    votingPrivateKey_ = privateKey;
}

void Member::unloadVotingPrivateKey() {
    votingPrivateKey_ = nullptr;
}

Member::Member(const Member& other)
    : id_(other.id_),
      type_(other.type_),
      name_(other.name_),
      email_(other.email_),
      publicKey_(other.publicKey_),
      keyPair_(other.keyPair_ ? std::make_unique<EcKeyPair>(EcKeyPair::fromPrivateKey(other.keyPair_->privateKey())) : nullptr),
      dateCreated_(other.dateCreated_),
      dateUpdated_(other.dateUpdated_),
      votingPublicKey_(other.votingPublicKey_),
      votingPrivateKey_(other.votingPrivateKey_) {}

Member& Member::operator=(const Member& other) {
    if (this != &other) {
        id_ = other.id_;
        type_ = other.type_;
        name_ = other.name_;
        email_ = other.email_;
        publicKey_ = other.publicKey_;
        keyPair_ = other.keyPair_ ? std::make_unique<EcKeyPair>(EcKeyPair::fromPrivateKey(other.keyPair_->privateKey())) : nullptr;
        dateCreated_ = other.dateCreated_;
        dateUpdated_ = other.dateUpdated_;
        votingPublicKey_ = other.votingPublicKey_;
        votingPrivateKey_ = other.votingPrivateKey_;
    }
    return *this;
}

std::string Member::toJson(bool includePrivateData) const {
    nlohmann::json j;
    
    // Public data
    j["id"] = idHex();
    j["type"] = static_cast<int>(type_);
    j["name"] = name_;
    j["email"] = email_;
    j["publicKey"] = nlohmann::json::array();
    for (auto b : publicKey_) {
        j["publicKey"].push_back(b);
    }
    j["dateCreated"] = dateCreated_;
    j["dateUpdated"] = dateUpdated_;
    
    // Voting public key
    if (votingPublicKey_) {
        j["votingPublicKey"]["n"] = votingPublicKey_->nHex();
        j["votingPublicKey"]["g"] = votingPublicKey_->gHex();
    }
    
    // Private data (only if requested and available)
    if (includePrivateData) {
        if (keyPair_) {
            auto priv = keyPair_->privateKey();
            j["privateKey"] = nlohmann::json::array();
            for (auto b : priv) {
                j["privateKey"].push_back(b);
            }
        }
        
        if (votingPrivateKey_) {
            j["votingPrivateKey"]["lambda"] = votingPrivateKey_->lambdaHex();
            j["votingPrivateKey"]["mu"] = votingPrivateKey_->muHex();
        }
    }
    
    return j.dump();
}

Member Member::fromJson(const std::string& json) {
    auto j = nlohmann::json::parse(json);
    
    auto type = static_cast<MemberType>(j["type"].get<int>());
    auto name = j["name"].get<std::string>();
    auto email = j["email"].get<std::string>();
    
    std::vector<uint8_t> publicKey;
    for (auto b : j["publicKey"]) {
        publicKey.push_back(b.get<uint8_t>());
    }
    
    Member member(type, name, email, publicKey, nullptr);
    
    // Restore private key if present
    if (j.contains("privateKey")) {
        std::vector<uint8_t> privateKey;
        for (auto b : j["privateKey"]) {
            privateKey.push_back(b.get<uint8_t>());
        }
        member.keyPair_ = std::make_unique<EcKeyPair>(EcKeyPair::fromPrivateKey(privateKey));
    }
    
    // Restore voting keys if present
    if (j.contains("votingPublicKey")) {
        auto n = hexToBytes(j["votingPublicKey"]["n"].get<std::string>());
        auto g = hexToBytes(j["votingPublicKey"]["g"].get<std::string>());
        member.votingPublicKey_ = std::make_shared<PaillierPublicKey>(n, g);
        
        if (j.contains("votingPrivateKey")) {
            auto lambda = hexToBytes(j["votingPrivateKey"]["lambda"].get<std::string>());
            auto mu = hexToBytes(j["votingPrivateKey"]["mu"].get<std::string>());
            member.votingPrivateKey_ = std::make_shared<PaillierPrivateKey>(lambda, mu, member.votingPublicKey_);
        }
    }
    
    member.dateCreated_ = j["dateCreated"].get<std::time_t>();
    member.dateUpdated_ = j["dateUpdated"].get<std::time_t>();
    
    return member;
}

} // namespace brightchain
