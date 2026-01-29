#include "brightchain/bulletin_board.hpp"
#include <openssl/sha.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace brightchain {

BulletinBoard::BulletinBoard(const Member& authority)
    : authority_(authority), sequence_(0) {}

BulletinBoardEntry BulletinBoard::publishVote(
    const std::vector<uint8_t>& pollId,
    const std::vector<std::vector<uint8_t>>& encryptedVote,
    const std::vector<uint8_t>& voterIdHash
) {
    uint64_t timestamp = getMicrosecondTimestamp();
    std::vector<uint8_t> merkleRoot = computeMerkleRootInternal(entries_);
    
    BulletinBoardEntry entry;
    entry.sequence = sequence_;
    entry.timestamp = timestamp;
    entry.pollId = pollId;
    entry.encryptedVote = encryptedVote;
    entry.voterIdHash = voterIdHash;
    entry.merkleRoot = merkleRoot;
    
    std::vector<uint8_t> entryData = serializeEntryData(entry);
    entry.entryHash = sha256(entryData);
    entry.signature = authority_.sign(entry.entryHash);
    
    sequence_++;
    entries_.push_back(entry);
    
    return entry;
}

TallyProof BulletinBoard::publishTally(
    const std::vector<uint8_t>& pollId,
    const std::vector<std::vector<uint8_t>>& tallies,
    const std::vector<std::string>& choices,
    const std::vector<std::vector<std::vector<uint8_t>>>& encryptedVotes
) {
    TallyProof proof;
    proof.pollId = pollId;
    proof.tallies = tallies;
    proof.choices = choices;
    proof.timestamp = getMicrosecondTimestamp();
    proof.votesHash = hashEncryptedVotes(encryptedVotes);
    proof.decryptionProof = generateDecryptionProof(encryptedVotes, tallies);
    
    std::vector<uint8_t> proofData = serializeTallyProof(proof);
    proof.signature = authority_.sign(proofData);
    
    tallyProofs_[toHex(pollId)] = proof;
    
    return proof;
}

std::vector<BulletinBoardEntry> BulletinBoard::getEntries(const std::vector<uint8_t>& pollId) const {
    std::vector<BulletinBoardEntry> result;
    std::string pollIdHex = toHex(pollId);
    
    for (const auto& entry : entries_) {
        if (toHex(entry.pollId) == pollIdHex) {
            result.push_back(entry);
        }
    }
    
    return result;
}

std::vector<BulletinBoardEntry> BulletinBoard::getAllEntries() const {
    return entries_;
}

const TallyProof* BulletinBoard::getTallyProof(const std::vector<uint8_t>& pollId) const {
    auto it = tallyProofs_.find(toHex(pollId));
    if (it != tallyProofs_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool BulletinBoard::verifyEntry(const BulletinBoardEntry& entry) const {
    std::vector<uint8_t> entryData = serializeEntryData(entry);
    std::vector<uint8_t> computedHash = sha256(entryData);
    
    if (!arraysEqual(computedHash, entry.entryHash)) {
        return false;
    }
    
    return authority_.verify(entry.entryHash, entry.signature);
}

bool BulletinBoard::verifyTallyProof(const TallyProof& proof) const {
    std::vector<uint8_t> proofData = serializeTallyProof(proof);
    return authority_.verify(proofData, proof.signature);
}

bool BulletinBoard::verifyMerkleTree() const {
    for (size_t i = 0; i < entries_.size(); i++) {
        const auto& entry = entries_[i];
        std::vector<BulletinBoardEntry> previousEntries(entries_.begin(), entries_.begin() + i);
        std::vector<uint8_t> expectedRoot = computeMerkleRootInternal(previousEntries);
        
        if (!arraysEqual(entry.merkleRoot, expectedRoot)) {
            return false;
        }
    }
    return true;
}

std::string BulletinBoard::computeMerkleRoot() const {
    if (entries_.empty()) {
        return std::string(64, '0');
    }
    
    // Compute current merkle root from all entries
    std::vector<uint8_t> root = computeMerkleRootInternal(entries_);
    return toHex(root);
}

std::vector<uint8_t> BulletinBoard::exportBoard() const {
    std::vector<std::vector<uint8_t>> parts;
    
    // Export entries
    parts.push_back(encodeNumber(entries_.size()));
    for (const auto& entry : entries_) {
        // Serialize entry (simplified - full implementation would match TypeScript)
        parts.push_back(entry.entryHash);
    }
    
    // Export tally proofs
    parts.push_back(encodeNumber(tallyProofs_.size()));
    for (const auto& [_, proof] : tallyProofs_) {
        parts.push_back(proof.signature);
    }
    
    return concat(parts);
}

uint64_t BulletinBoard::getMicrosecondTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    return static_cast<uint64_t>(micros.count());
}

std::vector<uint8_t> BulletinBoard::computeMerkleRootInternal(
    const std::vector<BulletinBoardEntry>& entries
) const {
    if (entries.empty()) {
        return std::vector<uint8_t>(32, 0);
    }
    
    std::vector<std::vector<uint8_t>> hashes;
    for (const auto& entry : entries) {
        hashes.push_back(entry.entryHash);
    }
    
    while (hashes.size() > 1) {
        std::vector<std::vector<uint8_t>> nextLevel;
        for (size_t i = 0; i < hashes.size(); i += 2) {
            if (i + 1 < hashes.size()) {
                nextLevel.push_back(sha256(concat({hashes[i], hashes[i + 1]})));
            } else {
                nextLevel.push_back(hashes[i]);
            }
        }
        hashes = nextLevel;
    }
    
    return hashes[0];
}

std::vector<uint8_t> BulletinBoard::hashEncryptedVotes(
    const std::vector<std::vector<std::vector<uint8_t>>>& votes
) const {
    std::vector<std::vector<uint8_t>> parts;
    for (const auto& vote : votes) {
        for (const auto& value : vote) {
            parts.push_back(value);
        }
    }
    return sha256(concat(parts));
}

std::vector<uint8_t> BulletinBoard::generateDecryptionProof(
    const std::vector<std::vector<std::vector<uint8_t>>>& encryptedVotes,
    const std::vector<std::vector<uint8_t>>& tallies
) const {
    // Simplified proof: hash of encrypted votes + tallies
    std::vector<std::vector<uint8_t>> parts;
    for (const auto& vote : encryptedVotes) {
        for (const auto& value : vote) {
            parts.push_back(value);
        }
    }
    for (const auto& tally : tallies) {
        parts.push_back(tally);
    }
    return sha256(concat(parts));
}

std::vector<uint8_t> BulletinBoard::serializeEntryData(const BulletinBoardEntry& entry) const {
    std::vector<std::vector<uint8_t>> parts;
    
    parts.push_back(encodeNumber(entry.sequence));
    parts.push_back(encodeNumber(entry.timestamp));
    parts.push_back(entry.pollId);
    parts.push_back(entry.voterIdHash);
    parts.push_back(entry.merkleRoot);
    
    for (const auto& value : entry.encryptedVote) {
        parts.push_back(value);
    }
    
    return concat(parts);
}

std::vector<uint8_t> BulletinBoard::serializeTallyProof(const TallyProof& proof) const {
    std::vector<std::vector<uint8_t>> parts;
    
    parts.push_back(proof.pollId);
    parts.push_back(encodeNumber(proof.timestamp));
    parts.push_back(proof.votesHash);
    parts.push_back(proof.decryptionProof);
    
    for (const auto& tally : proof.tallies) {
        parts.push_back(tally);
    }
    
    for (const auto& choice : proof.choices) {
        parts.push_back(encodeString(choice));
    }
    
    return concat(parts);
}

std::vector<uint8_t> BulletinBoard::sha256(const std::vector<uint8_t>& data) const {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), hash.data());
    return hash;
}

std::vector<uint8_t> BulletinBoard::encodeNumber(uint64_t n) const {
    std::vector<uint8_t> result(8);
    for (int i = 7; i >= 0; i--) {
        result[7 - i] = (n >> (i * 8)) & 0xFF;
    }
    return result;
}

std::vector<uint8_t> BulletinBoard::encodeBigInt(const std::vector<uint8_t>& bytes) const {
    return bytes; // Already in byte format
}

std::vector<uint8_t> BulletinBoard::encodeString(const std::string& s) const {
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::vector<uint8_t> BulletinBoard::concat(const std::vector<std::vector<uint8_t>>& arrays) const {
    size_t totalSize = 0;
    for (const auto& arr : arrays) {
        totalSize += arr.size();
    }
    
    std::vector<uint8_t> result;
    result.reserve(totalSize);
    
    for (const auto& arr : arrays) {
        result.insert(result.end(), arr.begin(), arr.end());
    }
    
    return result;
}

bool BulletinBoard::arraysEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) const {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

std::string BulletinBoard::toHex(const std::vector<uint8_t>& bytes) const {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

} // namespace brightchain
