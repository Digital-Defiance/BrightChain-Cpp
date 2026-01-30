#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace brightchain {

/**
 * Tally proof structure for cryptographic verification of vote counts
 */
struct TallyProof {
    /** Poll identifier */
    std::vector<uint8_t> pollId;
    
    /** Final tallies (as bytes, big-endian bigint representation) */
    std::vector<std::vector<uint8_t>> tallies;
    
    /** Choice names */
    std::vector<std::string> choices;
    
    /** Timestamp of tally (microseconds) */
    uint64_t timestamp;
    
    /** Hash of all encrypted votes */
    std::vector<uint8_t> votesHash;
    
    /** Cryptographic proof of correct decryption */
    std::vector<uint8_t> decryptionProof;
    
    /** Authority signature */
    std::vector<uint8_t> signature;
};

} // namespace brightchain
