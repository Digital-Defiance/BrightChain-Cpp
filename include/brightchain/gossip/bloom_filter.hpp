#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace brightchain::gossip {

/// Bloom filter using SHA3-512 (or SHA-512 fallback) truncated offsets for
/// independent hash functions.  Binary serialization: 4-byte LE size prefix
/// followed by the packed bit array.
class BloomFilter {
public:
    /// @param expectedItems  Hint used only for documentation / future sizing.
    /// @param falsePositiveRate  Desired false-positive rate (used to compute
    ///        bit-array size together with expectedItems).
    /// @param hashCount  Number of independent hash functions (k).
    BloomFilter(size_t expectedItems, double falsePositiveRate, int hashCount);

    /// Insert an item into the filter.
    void add(const std::string& item);

    /// Probabilistic membership test – may return true for items not added
    /// (false positive) but never returns false for items that were added.
    [[nodiscard]] bool mightContain(const std::string& item) const;

    /// Serialize to binary: 4-byte LE bit-count followed by packed bytes.
    [[nodiscard]] std::vector<uint8_t> serialize() const;

    /// Deserialize from the binary format produced by serialize().
    static BloomFilter deserialize(const std::vector<uint8_t>& data);

private:
    /// Compute the k hash indices for @p item.
    [[nodiscard]] std::vector<size_t> hashes(const std::string& item) const;

    /// Optimal bit-array size: m = -(n * ln(p)) / (ln2)^2
    static size_t optimalSize(size_t expectedItems, double falsePositiveRate);

    std::vector<bool> bits_;
    int hashCount_;
    size_t size_; // number of bits
};

} // namespace brightchain::gossip
