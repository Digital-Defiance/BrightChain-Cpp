#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace brightchain {

/**
 * Shamir's Secret Sharing implementation compatible with @digitaldefiance/secrets.
 * Uses Galois Field arithmetic in GF(2^bits).
 */
class ShamirSecretSharing {
public:
    /**
     * Initialize with specified bit length (default 8 bits = max 255 shares).
     * @param bits Number of bits for Galois Field (3-20)
     */
    explicit ShamirSecretSharing(uint8_t bits = 8);

    /**
     * Split a secret into shares.
     * @param secret Secret as hex string
     * @param numShares Total number of shares to create
     * @param threshold Minimum shares needed to reconstruct
     * @return Vector of share strings
     */
    std::vector<std::string> share(
        const std::string& secret,
        uint32_t numShares,
        uint32_t threshold
    );

    /**
     * Combine shares to reconstruct secret.
     * @param shares Vector of share strings
     * @return Reconstructed secret as hex string
     */
    std::string combine(const std::vector<std::string>& shares);

private:
    uint8_t bits_;
    uint32_t size_;
    uint32_t maxShares_;
    std::vector<uint32_t> logs_;
    std::vector<uint32_t> exps_;

    void initTables();
    uint32_t horner(uint32_t x, const std::vector<uint32_t>& coeffs);
    uint32_t lagrange(uint32_t at, const std::vector<uint32_t>& x, const std::vector<uint32_t>& y);
    std::string formatShare(uint32_t id, const std::string& data);
    void parseShare(const std::string& share, uint32_t& id, std::string& data);
    uint32_t getIdLength() const;
};

} // namespace brightchain
