#include "brightchain/shamir.hpp"
#include <openssl/rand.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <bitset>

namespace brightchain {

// Primitive polynomials for GF(2^bits)
static const uint32_t PRIMITIVE_POLYNOMIALS[] = {
    0, 0, 1, 3, 3, 5, 3, 3, 29, 17, 9, 5, 83, 27, 43, 3, 45, 9, 39, 39, 9
};

// Helper: hex string to binary string
static std::string hex2bin(const std::string& hex) {
    std::string bin;
    bin.reserve(hex.length() * 4);
    for (char c : hex) {
        int val;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') val = 10 + (c - 'A');
        else throw std::invalid_argument("Invalid hex character");
        
        bin += ((val >> 3) & 1) ? '1' : '0';
        bin += ((val >> 2) & 1) ? '1' : '0';
        bin += ((val >> 1) & 1) ? '1' : '0';
        bin += (val & 1) ? '1' : '0';
    }
    return bin;
}

// Helper: binary string to hex string
static std::string bin2hex(const std::string& bin) {
    std::string hex;
    // Pad to multiple of 4
    std::string padded = bin;
    while (padded.length() % 4 != 0) {
        padded = "0" + padded;
    }
    
    for (size_t i = 0; i < padded.length(); i += 4) {
        int val = 0;
        for (int j = 0; j < 4; ++j) {
            val = (val << 1) | (padded[i + j] == '1' ? 1 : 0);
        }
        hex += (val < 10) ? ('0' + val) : ('a' + val - 10);
    }
    return hex;
}

// Helper: pad binary string on left
static std::string padLeft(const std::string& str, size_t len) {
    if (str.length() >= len) return str;
    return std::string(len - str.length(), '0') + str;
}

// Helper: split binary string into int array (chunks of bits_ size from right to left)
static std::vector<uint32_t> splitNumStringToIntArray(const std::string& str, uint8_t bits, size_t padLength = 0) {
    std::string padded = padLength > 0 ? padLeft(str, padLength) : str;
    std::vector<uint32_t> parts;
    
    int i = padded.length();
    while (i > static_cast<int>(bits)) {
        i -= bits;
        uint32_t val = 0;
        for (int j = 0; j < bits; ++j) {
            val = (val << 1) | (padded[i + j] == '1' ? 1 : 0);
        }
        parts.push_back(val);
    }
    // Remaining bits
    if (i > 0) {
        uint32_t val = 0;
        for (int j = 0; j < i; ++j) {
            val = (val << 1) | (padded[j] == '1' ? 1 : 0);
        }
        parts.push_back(val);
    }
    
    return parts;
}

ShamirSecretSharing::ShamirSecretSharing(uint8_t bits) : bits_(bits) {
    if (bits < 3 || bits > 20) {
        throw std::invalid_argument("Bits must be between 3 and 20");
    }
    size_ = 1 << bits;  // 2^bits
    maxShares_ = size_ - 1;
    initTables();
}

void ShamirSecretSharing::initTables() {
    logs_.resize(size_);
    exps_.resize(size_);
    
    uint32_t x = 1;
    uint32_t primitive = PRIMITIVE_POLYNOMIALS[bits_];
    
    for (uint32_t i = 0; i < size_; ++i) {
        exps_[i] = x;
        logs_[x] = i;
        x <<= 1;
        if (x >= size_) {
            x ^= primitive;
            x &= maxShares_;
        }
    }
}

uint32_t ShamirSecretSharing::horner(uint32_t x, const std::vector<uint32_t>& coeffs) {
    uint32_t logx = logs_[x];
    uint32_t fx = 0;
    
    for (int i = coeffs.size() - 1; i >= 0; --i) {
        if (fx != 0) {
            fx = exps_[(logx + logs_[fx]) % maxShares_] ^ coeffs[i];
        } else {
            fx = coeffs[i];
        }
    }
    
    return fx;
}

uint32_t ShamirSecretSharing::lagrange(uint32_t at, const std::vector<uint32_t>& x, const std::vector<uint32_t>& y) {
    uint32_t sum = 0;
    size_t len = x.size();
    
    for (size_t i = 0; i < len; ++i) {
        if (y[i]) {
            int32_t product = logs_[y[i]];
            
            for (size_t j = 0; j < len; ++j) {
                if (i != j) {
                    if (at == x[j]) {
                        product = -1;
                        break;
                    }
                    product = (product + logs_[at ^ x[j]] - logs_[x[i] ^ x[j]] + maxShares_) % maxShares_;
                }
            }
            
            sum = (product == -1) ? sum : sum ^ exps_[product];
        }
    }
    
    return sum;
}

uint32_t ShamirSecretSharing::getIdLength() const {
    // ID length = number of hex chars needed to represent (2^bits - 1)
    // e.g., 8 bits: max=255=0xFF -> 2 chars, 10 bits: max=1023=0x3FF -> 3 chars
    uint32_t maxId = (1 << bits_) - 1;
    uint32_t idLen = 0;
    uint32_t temp = maxId;
    while (temp > 0) {
        ++idLen;
        temp >>= 4;  // Each hex digit is 4 bits
    }
    return idLen;
}

std::string ShamirSecretSharing::formatShare(uint32_t id, const std::string& data) {
    std::ostringstream oss;
    
    // Bits in base36 - single character
    char bitsChar;
    if (bits_ < 10) {
        bitsChar = '0' + bits_;
    } else {
        bitsChar = 'A' + (bits_ - 10);
    }
    oss << bitsChar;
    
    // ID in hex, padded to dynamic length based on bits
    uint32_t idLen = getIdLength();
    oss << std::hex << std::setfill('0') << std::setw(idLen) << id;
    
    oss << data;
    return oss.str();
}

void ShamirSecretSharing::parseShare(const std::string& share, uint32_t& id, std::string& data) {
    if (share.length() < 3) {
        throw std::invalid_argument("Invalid share format: too short");
    }
    
    // Parse bits (base36) - single character for bits 3-20
    char c = share[0];
    uint8_t shareBits;
    if (c >= '0' && c <= '9') {
        shareBits = c - '0';
    } else if (c >= 'A' && c <= 'Z') {
        shareBits = 10 + (c - 'A');
    } else if (c >= 'a' && c <= 'z') {
        shareBits = 10 + (c - 'a');
    } else {
        throw std::invalid_argument("Invalid bits character");
    }
    
    // Re-initialize if bits mismatch (like TS library does)
    if (shareBits != bits_) {
        // Reinitialize tables for new bit size
        bits_ = shareBits;
        size_ = 1 << bits_;
        maxShares_ = size_ - 1;
        initTables();
    }
    
    // Calculate ID length based on bits (after potential re-initialization)
    uint32_t idLen = getIdLength();
    
    if (share.length() < 1 + idLen) {
        throw std::invalid_argument("Invalid share format: too short for ID field");
    }
    
    // Parse ID (hex, dynamic length)
    std::string idStr = share.substr(1, idLen);
    try {
        id = std::stoul(idStr, nullptr, 16);
    } catch (...) {
        throw std::invalid_argument("Invalid share ID format: " + idStr);
    }
    
    // Rest is data
    data = share.substr(1 + idLen);
}

// Helper: pad binary string to length with leading zeros
static std::string padBinary(const std::string& bin, size_t multipleOf) {
    if (multipleOf == 0 || multipleOf == 1) {
        return bin;
    }
    
    size_t missing = bin.length() % multipleOf;
    if (missing == 0) {
        return bin;  // Already a multiple
    }
    
    // Pad to next multiple
    return std::string(multipleOf - missing, '0') + bin;
}

std::vector<std::string> ShamirSecretSharing::share(
    const std::string& secret,
    uint32_t numShares,
    uint32_t threshold
) {
    if (numShares < 2 || numShares > maxShares_) {
        throw std::invalid_argument("Invalid number of shares");
    }
    if (threshold < 2 || threshold > numShares) {
        throw std::invalid_argument("Invalid threshold");
    }
    
    // Convert hex secret to binary with leading "1" marker, then pad to multiple of 128 bits 
    // (like TypeScript: let secretBin = "1" + hex2bin(secret); then splitNumStringToIntArray pads)
    std::string secretBin = padBinary("1" + hex2bin(secret), 128);
    
    // Split into bits_-sized chunks
    std::vector<uint32_t> secretParts = splitNumStringToIntArray(secretBin, bits_);
    
    // Generate shares for each part
    std::vector<std::vector<uint32_t>> shareValues(numShares);
    
    for (uint32_t part : secretParts) {
        // Generate random coefficients
        std::vector<uint32_t> coeffs(threshold);
        coeffs[0] = part;
        
        for (uint32_t i = 1; i < threshold; ++i) {
            uint8_t randBytes[4];
            RAND_bytes(randBytes, 4);
            uint32_t randVal = (randBytes[0] | (randBytes[1] << 8) | 
                               (randBytes[2] << 16) | (randBytes[3] << 24)) % size_;
            coeffs[i] = randVal;
        }
        
        // Evaluate polynomial at points 1..numShares
        for (uint32_t i = 1; i <= numShares; ++i) {
            shareValues[i - 1].push_back(horner(i, coeffs));
        }
    }
    
    // Convert to share strings (binary concat then hex encode)
    std::vector<std::string> shares;
    for (uint32_t i = 0; i < numShares; ++i) {
        std::string binData;
        for (auto it = shareValues[i].rbegin(); it != shareValues[i].rend(); ++it) {
            binData += padLeft(std::bitset<32>(*it).to_string().substr(32 - bits_), bits_);
        }
        shares.push_back(formatShare(i + 1, bin2hex(binData)));
    }
    
    return shares;
}

std::string ShamirSecretSharing::combine(const std::vector<std::string>& shares) {
    if (shares.empty()) {
        throw std::invalid_argument("No shares provided");
    }
    
    // Parse shares and extract bits from first share
    std::vector<uint32_t> ids;
    std::vector<std::vector<uint32_t>> yValues;  // y[partIndex][shareIndex]
    
    for (const auto& share : shares) {
        uint32_t id;
        std::string data;
        parseShare(share, id, data);
        
        // Skip duplicates
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
            continue;
        }
        
        ids.push_back(id);
        
        // Convert hex data to binary, split into parts
        std::string binData = hex2bin(data);
        std::vector<uint32_t> parts = splitNumStringToIntArray(binData, bits_);
        
        // Accumulate into y values
        for (size_t j = 0; j < parts.size(); ++j) {
            if (yValues.size() <= j) {
                yValues.resize(j + 1);
            }
            yValues[j].push_back(parts[j]);
        }
    }
    
    // Reconstruct each part using Lagrange interpolation at x=0
    std::string resultBin;
    for (size_t i = 0; i < yValues.size(); ++i) {
        uint32_t val = lagrange(0, ids, yValues[i]);
        resultBin = padLeft(std::bitset<32>(val).to_string().substr(32 - bits_), bits_) + resultBin;
    }
    
    // Strip leading zeros AND the marker '1' (TypeScript: result.slice(result.indexOf("1") + 1))
    size_t firstOne = resultBin.find('1');
    if (firstOne != std::string::npos) {
        resultBin = resultBin.substr(firstOne + 1);  // Strip zeros AND the first '1' marker
    } else {
        resultBin = "0";  // All zeros
    }
    
    return bin2hex(resultBin);
}

} // namespace brightchain
