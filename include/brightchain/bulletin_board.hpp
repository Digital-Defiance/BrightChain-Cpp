#pragma once

#include "bulletin_board_entry.hpp"
#include "tally_proof.hpp"
#include "member.hpp"
#include <vector>
#include <map>
#include <memory>
#include <string>

namespace brightchain {

/**
 * Append-only public bulletin board with cryptographic verification
 * Implements requirement 1.2: Append-only, publicly verifiable vote publication
 */
class BulletinBoard {
public:
    /**
     * Constructor
     * @param authority Member who signs bulletin board entries
     */
    explicit BulletinBoard(const Member& authority);
    
    /**
     * Publish encrypted vote to bulletin board
     */
    BulletinBoardEntry publishVote(
        const std::vector<uint8_t>& pollId,
        const std::vector<std::vector<uint8_t>>& encryptedVote,
        const std::vector<uint8_t>& voterIdHash
    );
    
    /**
     * Publish tally with cryptographic proof
     */
    TallyProof publishTally(
        const std::vector<uint8_t>& pollId,
        const std::vector<std::vector<uint8_t>>& tallies,
        const std::vector<std::string>& choices,
        const std::vector<std::vector<std::vector<uint8_t>>>& encryptedVotes
    );
    
    /**
     * Get all entries for a poll
     */
    std::vector<BulletinBoardEntry> getEntries(const std::vector<uint8_t>& pollId) const;
    
    /**
     * Get all entries (entire bulletin board)
     */
    std::vector<BulletinBoardEntry> getAllEntries() const;
    
    /**
     * Get tally proof for a poll
     */
    const TallyProof* getTallyProof(const std::vector<uint8_t>& pollId) const;
    
    /**
     * Verify entry signature and hash
     */
    bool verifyEntry(const BulletinBoardEntry& entry) const;
    
    /**
     * Verify tally proof
     */
    bool verifyTallyProof(const TallyProof& proof) const;
    
    /**
     * Verify Merkle tree integrity
     */
    bool verifyMerkleTree() const;
    
    /**
     * Get current Merkle root as hex string
     */
    std::string computeMerkleRoot() const;
    
    /**
     * Export complete bulletin board for archival
     */
    std::vector<uint8_t> exportBoard() const;

private:
    std::vector<BulletinBoardEntry> entries_;
    std::map<std::string, TallyProof> tallyProofs_;
    Member authority_;
    uint64_t sequence_;
    
    uint64_t getMicrosecondTimestamp() const;
    std::vector<uint8_t> computeMerkleRootInternal(const std::vector<BulletinBoardEntry>& entries) const;
    std::vector<uint8_t> hashEncryptedVotes(const std::vector<std::vector<std::vector<uint8_t>>>& votes) const;
    std::vector<uint8_t> generateDecryptionProof(
        const std::vector<std::vector<std::vector<uint8_t>>>& encryptedVotes,
        const std::vector<std::vector<uint8_t>>& tallies
    ) const;
    std::vector<uint8_t> serializeEntryData(const BulletinBoardEntry& entry) const;
    std::vector<uint8_t> serializeTallyProof(const TallyProof& proof) const;
    std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> encodeNumber(uint64_t n) const;
    std::vector<uint8_t> encodeBigInt(const std::vector<uint8_t>& bytes) const;
    std::vector<uint8_t> encodeString(const std::string& s) const;
    std::vector<uint8_t> concat(const std::vector<std::vector<uint8_t>>& arrays) const;
    bool arraysEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) const;
    std::string toHex(const std::vector<uint8_t>& bytes) const;
};

} // namespace brightchain
