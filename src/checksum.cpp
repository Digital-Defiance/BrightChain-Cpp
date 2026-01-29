#include "brightchain/checksum.hpp"
#include <openssl/evp.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace brightchain {

Checksum::Checksum() : hash_{} {}

Checksum::Checksum(const HashArray& hash) : hash_(hash) {}

Checksum Checksum::fromData(const std::vector<uint8_t>& data) {
    HashArray hash;
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha3_512(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize SHA3-512");
    }

    if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to update SHA3-512");
    }

    unsigned int hashLen = 0;
    if (EVP_DigestFinal_ex(ctx, hash.data(), &hashLen) != 1 || hashLen != HASH_SIZE) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize SHA3-512");
    }

    EVP_MD_CTX_free(ctx);
    return Checksum(hash);
}

Checksum Checksum::fromHex(const std::string& hex) {
    if (hex.length() != HASH_SIZE * 2) {
        throw std::invalid_argument("Invalid hex string length");
    }

    HashArray hash;
    for (size_t i = 0; i < HASH_SIZE; ++i) {
        std::string byteStr = hex.substr(i * 2, 2);
        hash[i] = static_cast<uint8_t>(std::stoi(byteStr, nullptr, 16));
    }

    return Checksum(hash);
}

Checksum Checksum::fromHash(const HashArray& hash) {
    return Checksum(hash);
}

std::string Checksum::toHex() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (const auto& byte : hash_) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

bool Checksum::operator==(const Checksum& other) const {
    return hash_ == other.hash_;
}

bool Checksum::operator!=(const Checksum& other) const {
    return !(*this == other);
}

bool Checksum::operator<(const Checksum& other) const {
    return hash_ < other.hash_;
}

} // namespace brightchain

namespace std {
    size_t hash<brightchain::Checksum>::operator()(const brightchain::Checksum& checksum) const {
        // Use first 8 bytes of hash as size_t
        size_t result = 0;
        const auto& h = checksum.hash();
        std::memcpy(&result, h.data(), sizeof(size_t));
        return result;
    }
}
