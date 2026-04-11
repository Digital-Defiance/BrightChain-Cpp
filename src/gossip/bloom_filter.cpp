#include <brightchain/gossip/bloom_filter.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <openssl/evp.h>

namespace brightchain::gossip {

// ── helpers ────────────────────────────────────────────────────────────────

size_t BloomFilter::optimalSize(size_t expectedItems, double falsePositiveRate) {
    if (expectedItems == 0) {
        return 64; // sensible minimum
    }
    if (falsePositiveRate <= 0.0 || falsePositiveRate >= 1.0) {
        return expectedItems * 10; // fallback
    }
    // m = -(n * ln(p)) / (ln2)^2
    double m = -(static_cast<double>(expectedItems) * std::log(falsePositiveRate))
               / (std::log(2.0) * std::log(2.0));
    return std::max(static_cast<size_t>(std::ceil(m)), size_t{64});
}

// ── constructor ────────────────────────────────────────────────────────────

BloomFilter::BloomFilter(size_t expectedItems, double falsePositiveRate, int hashCount)
    : bits_(optimalSize(expectedItems, falsePositiveRate), false)
    , hashCount_(hashCount)
    , size_(bits_.size()) {
    if (hashCount_ < 1) {
        throw std::invalid_argument("BloomFilter hashCount must be >= 1");
    }
}

// ── hash computation using SHA3-512 (fallback SHA-512) ─────────────────────

std::vector<size_t> BloomFilter::hashes(const std::string& item) const {
    // Produce a 64-byte digest using SHA3-512 (preferred) or SHA-512 (fallback).
    unsigned char digest[64]{};
    unsigned int digestLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }

    // Try SHA3-512 first, fall back to SHA-512
    const EVP_MD* md = EVP_sha3_512();
    if (!md) {
        md = EVP_sha512();
    }
    if (!md) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("No suitable hash algorithm available");
    }

    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1 ||
        EVP_DigestUpdate(ctx, item.data(), item.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP digest computation failed");
    }
    EVP_MD_CTX_free(ctx);

    // Extract hashCount independent indices by reading 8-byte chunks at
    // different offsets within the 64-byte digest.  When hashCount exceeds
    // the number of non-overlapping 8-byte slots (8), we wrap around with
    // an XOR-fold using the slot index to maintain independence.
    std::vector<size_t> result;
    result.reserve(static_cast<size_t>(hashCount_));

    for (int i = 0; i < hashCount_; ++i) {
        uint64_t val = 0;
        size_t offset = static_cast<size_t>(i % 8) * 8; // 8 non-overlapping slots
        std::memcpy(&val, digest + offset, sizeof(val));

        // For indices beyond the first 8, mix in the slot index to
        // differentiate from earlier passes over the same offset.
        if (i >= 8) {
            val ^= static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ULL; // golden-ratio constant
        }

        result.push_back(static_cast<size_t>(val % size_));
    }

    return result;
}

// ── add / mightContain ─────────────────────────────────────────────────────

void BloomFilter::add(const std::string& item) {
    for (size_t idx : hashes(item)) {
        bits_[idx] = true;
    }
}

bool BloomFilter::mightContain(const std::string& item) const {
    for (size_t idx : hashes(item)) {
        if (!bits_[idx]) {
            return false;
        }
    }
    return true;
}

// ── serialization ──────────────────────────────────────────────────────────

std::vector<uint8_t> BloomFilter::serialize() const {
    // Format: 4-byte little-endian bit count + packed bytes (ceil(size_/8))
    std::vector<uint8_t> out;
    size_t packedBytes = (size_ + 7) / 8;
    out.resize(4 + packedBytes, 0);

    // Write size_ as 4-byte LE
    uint32_t sz = static_cast<uint32_t>(size_);
    out[0] = static_cast<uint8_t>(sz & 0xFF);
    out[1] = static_cast<uint8_t>((sz >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((sz >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((sz >> 24) & 0xFF);

    // Pack bits into bytes (LSB first within each byte)
    for (size_t i = 0; i < size_; ++i) {
        if (bits_[i]) {
            out[4 + i / 8] |= static_cast<uint8_t>(1u << (i % 8));
        }
    }

    return out;
}

BloomFilter BloomFilter::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 4) {
        throw std::invalid_argument("BloomFilter data too short for header");
    }

    uint32_t sz = static_cast<uint32_t>(data[0])
                | (static_cast<uint32_t>(data[1]) << 8)
                | (static_cast<uint32_t>(data[2]) << 16)
                | (static_cast<uint32_t>(data[3]) << 24);

    size_t bitCount = static_cast<size_t>(sz);
    size_t packedBytes = (bitCount + 7) / 8;

    if (data.size() < 4 + packedBytes) {
        throw std::invalid_argument("BloomFilter data too short for bit array");
    }

    // We don't know the original expectedItems / falsePositiveRate, so we
    // construct with dummy values and then overwrite the internals.
    BloomFilter bf(0, 0.01, 1); // dummy – will be replaced
    bf.size_ = bitCount;
    bf.bits_.assign(bitCount, false);
    // hashCount_ is transmitted separately per the design doc; default to 7
    // (the protocol default).  Callers can override if needed.
    bf.hashCount_ = 7;

    for (size_t i = 0; i < bitCount; ++i) {
        if (data[4 + i / 8] & (1u << (i % 8))) {
            bf.bits_[i] = true;
        }
    }

    return bf;
}

} // namespace brightchain::gossip
