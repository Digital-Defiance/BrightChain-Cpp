#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <memory>

namespace brightchain {

/**
 * Checksum class using SHA3-512.
 * Provides hash generation, comparison, and serialization.
 */
class Checksum {
public:
    static constexpr size_t HASH_SIZE = 64; // SHA3-512 produces 64 bytes
    using HashArray = std::array<uint8_t, HASH_SIZE>;

    /**
     * Create checksum from data.
     * @param data The data to hash
     * @return Checksum object
     */
    static Checksum fromData(const std::vector<uint8_t>& data);

    /**
     * Create checksum from hex string.
     * @param hex Hex string representation
     * @return Checksum object
     */
    static Checksum fromHex(const std::string& hex);

    /**
     * Create checksum from raw hash bytes.
     * @param hash The hash bytes
     * @return Checksum object
     */
    static Checksum fromHash(const HashArray& hash);

    /**
     * Default constructor (zero hash).
     */
    Checksum();

    /**
     * Convert to hex string.
     * @return Hex string representation
     */
    std::string toHex() const;

    /**
     * Get raw hash bytes.
     * @return Hash bytes
     */
    const HashArray& hash() const { return hash_; }

    /**
     * Comparison operators.
     */
    bool operator==(const Checksum& other) const;
    bool operator!=(const Checksum& other) const;
    bool operator<(const Checksum& other) const;

private:
    explicit Checksum(const HashArray& hash);
    HashArray hash_;
};

} // namespace brightchain

// Hash function for std::unordered_map
namespace std {
    template<>
    struct hash<brightchain::Checksum> {
        size_t operator()(const brightchain::Checksum& checksum) const;
    };
}
