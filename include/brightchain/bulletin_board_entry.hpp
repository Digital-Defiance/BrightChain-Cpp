#pragma once

#include <vector>
#include <cstdint>

namespace brightchain {

/**
 * Bulletin board entry structure for published votes
 */
struct BulletinBoardEntry {
    /** Sequence number (monotonically increasing) */
    uint64_t sequence;
    
    /** Microsecond-precision timestamp */
    uint64_t timestamp;
    
    /** Poll identifier */
    std::vector<uint8_t> pollId;
    
    /** Encrypted vote data (as bytes, big-endian bigint representation) */
    std::vector<std::vector<uint8_t>> encryptedVote;
    
    /** Hash of voter ID (anonymized) */
    std::vector<uint8_t> voterIdHash;
    
    /** Merkle root of all entries up to this point */
    std::vector<uint8_t> merkleRoot;
    
    /** Hash of this entry */
    std::vector<uint8_t> entryHash;
    
    /** Authority signature */
    std::vector<uint8_t> signature;
};

} // namespace brightchain
