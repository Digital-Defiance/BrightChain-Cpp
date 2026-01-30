#pragma once

#include <vector>
#include <optional>
#include <cstdint>

namespace brightchain {

/**
 * Encrypted vote data using Paillier homomorphic encryption.
 * Structure varies by voting method.
 */
struct EncryptedVote {
    // Single choice index (for Plurality, Weighted, etc.)
    std::optional<int> choiceIndex;
    
    // Multiple choice indices (for Approval voting)
    std::optional<std::vector<int>> choices;
    
    // Ranked choice indices in preference order (for RCV, Borda)
    std::optional<std::vector<int>> rankings;
    
    // Vote weight (for Weighted voting)
    std::optional<std::vector<uint8_t>> weight;  // bigint as bytes
    
    // Score value 0-10 (for Score voting)
    std::optional<int> score;
    
    // Array of encrypted vote values (one per choice) - bigints as bytes
    std::vector<std::vector<uint8_t>> encrypted;
};

} // namespace brightchain
