#include <brightchain/audit_log.hpp>
#include <brightchain/checksum.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace brightchain {

AuditLog::AuditLog(const Member& authority) : authority_(authority), sequence_(0) {}

AuditEntry AuditLog::recordPollCreated(const std::vector<uint8_t>& pollId, const std::map<std::string, std::string>& metadata) {
    return appendEntry(AuditEventType::PollCreated, pollId, std::nullopt, authority_.idBytes(), metadata);
}

AuditEntry AuditLog::recordVoteCast(const std::vector<uint8_t>& pollId, const std::vector<uint8_t>& voterIdHash) {
    return appendEntry(AuditEventType::VoteCast, pollId, voterIdHash);
}

AuditEntry AuditLog::recordPollClosed(const std::vector<uint8_t>& pollId, const std::map<std::string, std::string>& metadata) {
    return appendEntry(AuditEventType::PollClosed, pollId, std::nullopt, authority_.idBytes(), metadata);
}

std::vector<AuditEntry> AuditLog::getEntriesForPoll(const std::vector<uint8_t>& pollId) const {
    std::vector<AuditEntry> result;
    for (const auto& entry : entries_) {
        if (entry.pollId == pollId) {
            result.push_back(entry);
        }
    }
    return result;
}

bool AuditLog::verifyChain() const {
    if (entries_.empty()) return true;
    
    for (size_t i = 0; i < entries_.size(); i++) {
        const auto& entry = entries_[i];
        auto computedHash = computeEntryHash(entry);
        if (computedHash != entry.entryHash) return false;
        if (!verifyEntry(entry)) return false;
        if (i > 0 && entry.previousHash != entries_[i-1].entryHash) return false;
    }
    return true;
}

bool AuditLog::verifyEntry(const AuditEntry& entry) const {
    auto data = serializeForSigning(entry);
    return authority_.verify(data, entry.signature);
}

AuditEntry AuditLog::appendEntry(AuditEventType type, const std::vector<uint8_t>& pollId,
                                 const std::optional<std::vector<uint8_t>>& voterIdHash,
                                 const std::optional<std::vector<uint8_t>>& authorityId,
                                 const std::map<std::string, std::string>& metadata) {
    AuditEntry entry;
    entry.sequence = sequence_++;
    entry.eventType = type;
    entry.timestamp = getMicrosecondTimestamp();
    entry.pollId = pollId;
    entry.voterIdHash = voterIdHash;
    entry.authorityId = authorityId;
    entry.previousHash = entries_.empty() ? std::vector<uint8_t>(32, 0) : entries_.back().entryHash;
    if (!metadata.empty()) entry.metadata = metadata;
    
    entry.entryHash = computeEntryHash(entry);
    auto sigData = serializeForSigning(entry);
    entry.signature = authority_.sign(sigData);
    
    entries_.push_back(entry);
    return entry;
}

std::vector<uint8_t> AuditLog::computeEntryHash(const AuditEntry& entry) const {
    auto data = serializeForHashing(entry);
    auto checksum = Checksum::fromData(data);
    return std::vector<uint8_t>(checksum.hash().begin(), checksum.hash().end());
}

std::vector<uint8_t> AuditLog::serializeForHashing(const AuditEntry& entry) const {
    std::vector<uint8_t> result;
    
    // Sequence
    for (int i = 0; i < 8; i++) {
        result.push_back((entry.sequence >> (i * 8)) & 0xFF);
    }
    
    // Event type
    result.push_back(static_cast<uint8_t>(entry.eventType));
    
    // Timestamp
    for (int i = 0; i < 8; i++) {
        result.push_back((entry.timestamp >> (i * 8)) & 0xFF);
    }
    
    // Poll ID
    result.insert(result.end(), entry.pollId.begin(), entry.pollId.end());
    
    // Previous hash
    result.insert(result.end(), entry.previousHash.begin(), entry.previousHash.end());
    
    // Optional fields
    if (entry.voterIdHash) {
        result.insert(result.end(), entry.voterIdHash->begin(), entry.voterIdHash->end());
    }
    if (entry.authorityId) {
        result.insert(result.end(), entry.authorityId->begin(), entry.authorityId->end());
    }
    
    return result;
}

std::vector<uint8_t> AuditLog::serializeForSigning(const AuditEntry& entry) const {
    auto hashData = serializeForHashing(entry);
    hashData.insert(hashData.end(), entry.entryHash.begin(), entry.entryHash.end());
    return hashData;
}

int64_t AuditLog::getMicrosecondTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

} // namespace brightchain
