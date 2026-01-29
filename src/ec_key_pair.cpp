#include "brightchain/ec_key_pair.hpp"
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace brightchain {

EcKeyPair::EcKeyPair(EC_KEY* key) : key_(key) {}

EcKeyPair::~EcKeyPair() {
    if (key_) {
        EC_KEY_free(key_);
    }
}

EcKeyPair::EcKeyPair(EcKeyPair&& other) noexcept : key_(other.key_) {
    other.key_ = nullptr;
}

EcKeyPair& EcKeyPair::operator=(EcKeyPair&& other) noexcept {
    if (this != &other) {
        if (key_) {
            EC_KEY_free(key_);
        }
        key_ = other.key_;
        other.key_ = nullptr;
    }
    return *this;
}

EcKeyPair EcKeyPair::generate() {
    EC_KEY* key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!key) {
        throw std::runtime_error("Failed to create EC key");
    }

    if (EC_KEY_generate_key(key) != 1) {
        EC_KEY_free(key);
        throw std::runtime_error("Failed to generate EC key");
    }

    return EcKeyPair(key);
}

EcKeyPair EcKeyPair::fromPrivateKey(const std::vector<uint8_t>& privateKey) {
    if (privateKey.size() != 32) {
        throw std::invalid_argument("Private key must be 32 bytes");
    }

    EC_KEY* key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!key) {
        throw std::runtime_error("Failed to create EC key");
    }

    BIGNUM* bn = BN_bin2bn(privateKey.data(), privateKey.size(), nullptr);
    if (!bn || EC_KEY_set_private_key(key, bn) != 1) {
        BN_free(bn);
        EC_KEY_free(key);
        throw std::runtime_error("Failed to set private key");
    }

    const EC_GROUP* group = EC_KEY_get0_group(key);
    EC_POINT* pub_key = EC_POINT_new(group);
    if (!pub_key || EC_POINT_mul(group, pub_key, bn, nullptr, nullptr, nullptr) != 1) {
        EC_POINT_free(pub_key);
        BN_free(bn);
        EC_KEY_free(key);
        throw std::runtime_error("Failed to compute public key");
    }

    EC_KEY_set_public_key(key, pub_key);
    EC_POINT_free(pub_key);
    BN_free(bn);

    return EcKeyPair(key);
}

EcKeyPair EcKeyPair::fromPrivateKeyHex(const std::string& privateKeyHex) {
    if (privateKeyHex.length() != 64) {
        throw std::invalid_argument("Private key hex must be 64 characters");
    }

    std::vector<uint8_t> privateKey(32);
    for (size_t i = 0; i < 32; ++i) {
        privateKey[i] = std::stoi(privateKeyHex.substr(i * 2, 2), nullptr, 16);
    }

    return fromPrivateKey(privateKey);
}

std::vector<uint8_t> EcKeyPair::publicKey() const {
    const EC_POINT* pub = EC_KEY_get0_public_key(key_);
    const EC_GROUP* group = EC_KEY_get0_group(key_);

    size_t len = EC_POINT_point2oct(group, pub, POINT_CONVERSION_COMPRESSED, nullptr, 0, nullptr);
    std::vector<uint8_t> result(len);
    EC_POINT_point2oct(group, pub, POINT_CONVERSION_COMPRESSED, result.data(), len, nullptr);

    return result;
}

std::vector<uint8_t> EcKeyPair::privateKey() const {
    const BIGNUM* priv = EC_KEY_get0_private_key(key_);
    std::vector<uint8_t> result(32);
    BN_bn2binpad(priv, result.data(), 32);
    return result;
}

std::string EcKeyPair::publicKeyHex() const {
    auto pub = publicKey();
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto byte : pub) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::string EcKeyPair::privateKeyHex() const {
    auto priv = privateKey();
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto byte : priv) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> EcKeyPair::sign(const std::vector<uint8_t>& data) const {
    unsigned int sig_len = ECDSA_size(key_);
    std::vector<uint8_t> signature(sig_len);

    if (ECDSA_sign(0, data.data(), data.size(), signature.data(), &sig_len, key_) != 1) {
        throw std::runtime_error("Failed to sign data");
    }

    signature.resize(sig_len);
    return signature;
}

bool EcKeyPair::verify(
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& signature,
    const std::vector<uint8_t>& publicKey
) {
    EC_KEY* key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!key) {
        return false;
    }

    const EC_GROUP* group = EC_KEY_get0_group(key);
    EC_POINT* pub = EC_POINT_new(group);
    if (!pub || EC_POINT_oct2point(group, pub, publicKey.data(), publicKey.size(), nullptr) != 1) {
        EC_POINT_free(pub);
        EC_KEY_free(key);
        return false;
    }

    EC_KEY_set_public_key(key, pub);
    int result = ECDSA_verify(0, data.data(), data.size(), signature.data(), signature.size(), key);

    EC_POINT_free(pub);
    EC_KEY_free(key);

    return result == 1;
}

} // namespace brightchain
